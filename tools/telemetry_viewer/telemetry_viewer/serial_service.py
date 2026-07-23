from __future__ import annotations

import asyncio
import contextlib
import csv
import io
import math
from collections import deque
from collections.abc import Awaitable, Callable
from datetime import datetime, timezone
from typing import Any

import serial
from serial.tools import list_ports

from .protocol import (
    FIELD_NAMES,
    INT32_MIN,
    TelemetryStreamParser,
    build_frame,
    bytes_to_ascii,
    bytes_to_hex,
    utc_now_iso,
)


PublishCallback = Callable[[dict[str, Any]], Awaitable[None]]


def list_serial_ports() -> list[dict[str, Any]]:
    ports = []
    for port in sorted(list_ports.comports(), key=lambda item: item.device):
        ports.append(
            {
                "device": port.device,
                "name": port.name,
                "description": port.description,
                "hwid": port.hwid,
                "vid": port.vid,
                "pid": port.pid,
                "serial_number": port.serial_number,
                "manufacturer": port.manufacturer,
                "product": port.product,
            }
        )
    return ports


class TelemetryMonitor:
    def __init__(self, publish: PublishCallback, *, max_records: int = 1000) -> None:
        self.publish = publish
        self.parser = TelemetryStreamParser()
        self.records: deque[dict[str, Any]] = deque(maxlen=max_records)
        self.serial_port: serial.Serial | None = None
        self.worker_task: asyncio.Task[None] | None = None
        self.mode = "disconnected"
        self.connection: dict[str, Any] | None = None
        self.event_id = 0
        self.reset_statistics()

    def reset_statistics(self) -> None:
        self.rx_bytes = 0
        self.raw_chunk_count = 0
        self.frame_count = 0
        self.valid_frame_count = 0
        self.invalid_frame_count = 0
        self.noise_bytes = 0
        self.noise_record_count = 0
        self.latest_frame: dict[str, Any] | None = None
        self.last_valid_tick: int | None = None
        self.last_valid_host_time: datetime | None = None
        self.last_device_interval_ms: int | None = None
        self.last_host_interval_ms: float | None = None
        self.started_at = utc_now_iso()

    async def connect(
        self,
        *,
        port: str,
        baud_rate: int = 9600,
        data_bits: int = 8,
        parity: str = "N",
        stop_bits: float = 1,
    ) -> None:
        await self.disconnect()
        bytesize = {
            5: serial.FIVEBITS,
            6: serial.SIXBITS,
            7: serial.SEVENBITS,
            8: serial.EIGHTBITS,
        }[data_bits]
        parity_value = {
            "N": serial.PARITY_NONE,
            "E": serial.PARITY_EVEN,
            "O": serial.PARITY_ODD,
        }[parity]
        stopbits_value = {
            1: serial.STOPBITS_ONE,
            1.5: serial.STOPBITS_ONE_POINT_FIVE,
            2: serial.STOPBITS_TWO,
        }[stop_bits]

        self.serial_port = await asyncio.to_thread(
            serial.Serial,
            port=port,
            baudrate=baud_rate,
            bytesize=bytesize,
            parity=parity_value,
            stopbits=stopbits_value,
            timeout=0.1,
            write_timeout=0.5,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        self.parser.reset()
        self.mode = "serial"
        self.connection = {
            "port": port,
            "baud_rate": baud_rate,
            "data_bits": data_bits,
            "parity": parity,
            "stop_bits": stop_bits,
        }
        self.worker_task = asyncio.create_task(self._serial_read_loop(), name="serial-read-loop")
        await self._publish_status()

    async def start_demo(self, *, period_ms: int = 1000, inject_noise: bool = True) -> None:
        await self.disconnect()
        self.parser.reset()
        self.mode = "demo"
        self.connection = {
            "port": "DEMO",
            "baud_rate": 9600,
            "data_bits": 8,
            "parity": "N",
            "stop_bits": 1,
            "period_ms": period_ms,
            "inject_noise": inject_noise,
        }
        self.worker_task = asyncio.create_task(
            self._demo_loop(period_ms=period_ms, inject_noise=inject_noise),
            name="telemetry-demo-loop",
        )
        await self._publish_status()

    async def disconnect(self) -> None:
        task = self.worker_task
        self.worker_task = None
        if task is not None and task is not asyncio.current_task():
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task

        active_port = self.serial_port
        self.serial_port = None
        if active_port is not None:
            with contextlib.suppress(Exception):
                await asyncio.to_thread(active_port.close)

        for parser_event in self.parser.drain():
            await self._handle_parser_event(parser_event)
        self.mode = "disconnected"
        self.connection = None
        await self._publish_status()

    async def clear(self) -> None:
        self.records.clear()
        self.parser.reset()
        self.reset_statistics()
        await self.publish({"type": "cleared", "snapshot": self.snapshot()})

    def snapshot(self, *, record_limit: int = 150) -> dict[str, Any]:
        return {
            "status": self.status(),
            "latest_frame": self.latest_frame,
            "records": list(self.records)[-record_limit:],
        }

    def status(self) -> dict[str, Any]:
        return {
            "mode": self.mode,
            "connected": self.mode != "disconnected",
            "connection": self.connection,
            "started_at": self.started_at,
            "rx_bytes": self.rx_bytes,
            "raw_chunk_count": self.raw_chunk_count,
            "frame_count": self.frame_count,
            "valid_frame_count": self.valid_frame_count,
            "invalid_frame_count": self.invalid_frame_count,
            "noise_bytes": self.noise_bytes,
            "noise_record_count": self.noise_record_count,
            "last_device_interval_ms": self.last_device_interval_ms,
            "last_host_interval_ms": self.last_host_interval_ms,
            "record_count": len(self.records),
        }

    def export_csv(self) -> str:
        output = io.StringIO(newline="")
        fieldnames = [
            "received_at",
            "valid",
            "checksum_valid",
            "device_interval_ms",
            "host_interval_ms",
            *FIELD_NAMES,
            "supplied_checksum_hex",
            "calculated_checksum_hex",
            "errors",
            "raw_ascii",
            "raw_hex",
        ]
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        for record in self.records:
            if record["kind"] != "frame":
                continue
            frame = record["frame"]
            row = {
                "received_at": frame["received_at"],
                "valid": frame["valid"],
                "checksum_valid": frame["checksum_valid"],
                "device_interval_ms": frame.get("device_interval_ms"),
                "host_interval_ms": frame.get("host_interval_ms"),
                "supplied_checksum_hex": frame["supplied_checksum_hex"],
                "calculated_checksum_hex": frame["calculated_checksum_hex"],
                "errors": "; ".join(frame["errors"]),
                "raw_ascii": frame["raw_ascii"],
                "raw_hex": frame["raw_hex"],
                **frame["values"],
            }
            writer.writerow(row)
        return output.getvalue()

    async def _serial_read_loop(self) -> None:
        try:
            while self.serial_port is not None and self.serial_port.is_open:
                chunk = await asyncio.to_thread(self.serial_port.read, 256)
                if chunk:
                    await self._handle_chunk(chunk)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            await self.publish(
                {
                    "type": "error",
                    "message": f"串口读取失败: {exc}",
                    "at": utc_now_iso(),
                }
            )
        finally:
            if self.mode == "serial":
                self.mode = "disconnected"
                self.connection = None
                await self._publish_status()

    async def _demo_loop(self, *, period_ms: int, inject_noise: bool) -> None:
        tick_ms = 0
        sequence = 0
        try:
            while True:
                loop_started = asyncio.get_running_loop().time()
                tick_ms += period_ms
                sequence += 1
                phase = sequence / 5.0
                values = {
                    "tick_ms": tick_ms,
                    "pressure_online": 1,
                    "pressure_status": 1,
                    "pressure_read_mode": 0,
                    "pressure_float_valid": 0,
                    "pressure_raw": int(1024 + math.sin(phase) * 80),
                    "pressure_value_x1000": INT32_MIN,
                    "pressure_unit_code": 0,
                    "pressure_decimal_point": 2,
                    "xda_online": 1,
                    "xda_status": 1,
                    "ec_x100": int(255 + math.sin(phase / 2) * 35),
                    "temperature_x10": int(245 + math.cos(phase / 3) * 12),
                    "tds_ppm": int(410 + math.sin(phase / 2) * 25),
                    "salinity_ppm": int(12 + math.sin(phase / 4) * 3),
                }
                frame = build_frame(values)

                if inject_noise and sequence % 5 == 1:
                    await self._handle_chunk(b"\x02\x03\x00\x00\x00\x04\x44\x3A\xFF\x00")
                    await asyncio.sleep(0.012)

                cut_a = min(11, len(frame))
                cut_b = min(32, len(frame))
                await self._handle_chunk(frame[:cut_a])
                await asyncio.sleep(0.009)
                await self._handle_chunk(frame[cut_a:cut_b])
                await asyncio.sleep(0.024)
                await self._handle_chunk(frame[cut_b:])

                elapsed = (asyncio.get_running_loop().time() - loop_started) * 1000
                await asyncio.sleep(max(0.02, (period_ms - elapsed) / 1000))
        except asyncio.CancelledError:
            raise

    async def _handle_chunk(self, chunk: bytes) -> None:
        received_at = utc_now_iso()
        self.rx_bytes += len(chunk)
        self.raw_chunk_count += 1

        raw_record = self._new_record(
            "raw",
            received_at=received_at,
            byte_count=len(chunk),
            raw_hex=bytes_to_hex(chunk),
            raw_ascii=bytes_to_ascii(chunk),
        )
        self.records.append(raw_record)
        await self.publish({"type": "record", "record": raw_record})

        for parser_event in self.parser.feed(chunk, received_at=received_at):
            await self._handle_parser_event(parser_event)

        await self._publish_status()

    async def _handle_parser_event(self, parser_event: Any) -> None:
        if parser_event.kind == "noise":
            self.noise_bytes += len(parser_event.raw)
            self.noise_record_count += 1
            record = self._new_record(
                "noise",
                received_at=utc_now_iso(),
                byte_count=len(parser_event.raw),
                raw_hex=bytes_to_hex(parser_event.raw),
                raw_ascii=bytes_to_ascii(parser_event.raw),
                reason=parser_event.reason,
            )
        else:
            frame = parser_event.frame
            assert frame is not None
            self.frame_count += 1
            if frame["valid"]:
                self.valid_frame_count += 1
                self._add_intervals(frame)
                self.latest_frame = frame
            else:
                self.invalid_frame_count += 1
            record = self._new_record("frame", received_at=frame["received_at"], frame=frame)

        self.records.append(record)
        await self.publish({"type": "record", "record": record})

    def _add_intervals(self, frame: dict[str, Any]) -> None:
        tick_ms = frame["values"].get("tick_ms")
        host_time = datetime.fromisoformat(frame["received_at"])
        if tick_ms is not None and self.last_valid_tick is not None:
            self.last_device_interval_ms = (tick_ms - self.last_valid_tick) & 0xFFFFFFFF
        else:
            self.last_device_interval_ms = None

        if self.last_valid_host_time is not None:
            self.last_host_interval_ms = round(
                (host_time - self.last_valid_host_time).total_seconds() * 1000,
                3,
            )
        else:
            self.last_host_interval_ms = None

        frame["device_interval_ms"] = self.last_device_interval_ms
        frame["host_interval_ms"] = self.last_host_interval_ms
        self.last_valid_tick = tick_ms
        self.last_valid_host_time = host_time

    def _new_record(self, kind: str, **payload: Any) -> dict[str, Any]:
        self.event_id += 1
        return {
            "id": self.event_id,
            "kind": kind,
            **payload,
        }

    async def _publish_status(self) -> None:
        await self.publish({"type": "status", "status": self.status()})

