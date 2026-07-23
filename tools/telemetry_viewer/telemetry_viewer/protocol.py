from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Iterable, Mapping


START_MARKER = b"$BE,"
LINE_ENDING = b"\r\n"
INT32_MIN = -(2**31)
MAX_FRAME_BYTES = 256

FIELD_NAMES = (
    "tick_ms",
    "pressure_online",
    "pressure_status",
    "pressure_read_mode",
    "pressure_float_valid",
    "pressure_raw",
    "pressure_value_x1000",
    "pressure_unit_code",
    "pressure_decimal_point",
    "xda_online",
    "xda_status",
    "ec_x100",
    "temperature_x10",
    "tds_ppm",
    "salinity_ppm",
)

STATUS_NAMES = {
    0: "IDLE",
    1: "OK",
    2: "TIMEOUT",
    3: "CRC_ERROR",
    4: "FRAME_ERROR",
    5: "UART_ERROR",
    6: "MODBUS_EXCEPTION",
}

READ_MODE_NAMES = {
    0: "RAW",
    1: "FLOAT",
}


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def calculate_xor(payload: bytes | bytearray | memoryview) -> int:
    checksum = 0
    for value in payload:
        checksum ^= int(value)
    return checksum


def bytes_to_hex(data: bytes | bytearray | memoryview) -> str:
    return " ".join(f"{value:02X}" for value in data)


def bytes_to_ascii(data: bytes | bytearray | memoryview) -> str:
    output: list[str] = []
    for value in data:
        if value == 0x0D:
            output.append("\\r")
        elif value == 0x0A:
            output.append("\\n")
        elif 0x20 <= value <= 0x7E:
            output.append(chr(value))
        else:
            output.append(f"\\x{value:02X}")
    return "".join(output)


def _status_name(value: int) -> str:
    return STATUS_NAMES.get(value, f"UNKNOWN({value})")


def _derive_values(values: Mapping[str, int]) -> dict[str, Any]:
    decimal_point = values["pressure_decimal_point"]
    raw_scaled: float | None = None
    if 0 <= decimal_point <= 9:
        raw_scaled = values["pressure_raw"] / (10**decimal_point)

    pressure_value: float | None = None
    if (
        values["pressure_float_valid"] != 0
        and values["pressure_value_x1000"] != INT32_MIN
    ):
        pressure_value = values["pressure_value_x1000"] / 1000.0

    return {
        "tick_seconds": values["tick_ms"] / 1000.0,
        "pressure_status_text": _status_name(values["pressure_status"]),
        "pressure_read_mode_text": READ_MODE_NAMES.get(
            values["pressure_read_mode"],
            f"UNKNOWN({values['pressure_read_mode']})",
        ),
        "pressure_raw_scaled": raw_scaled,
        "pressure_value": pressure_value,
        "xda_status_text": _status_name(values["xda_status"]),
        "ec_value": values["ec_x100"] / 100.0,
        "temperature_c": values["temperature_x10"] / 10.0,
    }


def parse_frame(raw: bytes, *, received_at: str | None = None) -> dict[str, Any]:
    errors: list[str] = []
    payload_text: str | None = None
    supplied_checksum: int | None = None
    calculated_checksum: int | None = None
    values: dict[str, int] = {}

    if not raw.endswith(LINE_ENDING):
        errors.append("missing CRLF terminator")
        body = raw
    else:
        body = raw[: -len(LINE_ENDING)]

    if not body.startswith(b"$"):
        errors.append("missing '$' start byte")

    star_index = body.rfind(b"*")
    if star_index < 0:
        errors.append("missing checksum separator '*'")
        payload_bytes = body[1:] if body.startswith(b"$") else body
        checksum_bytes = b""
    else:
        payload_bytes = body[1:star_index] if body.startswith(b"$") else body[:star_index]
        checksum_bytes = body[star_index + 1 :]

    try:
        payload_text = payload_bytes.decode("ascii")
    except UnicodeDecodeError:
        errors.append("payload contains non-ASCII bytes")

    if len(checksum_bytes) != 2:
        errors.append("checksum must contain exactly two hexadecimal digits")
    else:
        try:
            supplied_checksum = int(checksum_bytes.decode("ascii"), 16)
        except (UnicodeDecodeError, ValueError):
            errors.append("checksum is not valid hexadecimal")

    if payload_bytes:
        calculated_checksum = calculate_xor(payload_bytes)
    if supplied_checksum is not None and calculated_checksum != supplied_checksum:
        errors.append(
            f"checksum mismatch: received {supplied_checksum:02X}, "
            f"calculated {calculated_checksum:02X}"
        )

    if payload_text is not None:
        tokens = payload_text.split(",")
        if not tokens or tokens[0] != "BE":
            errors.append("payload identifier must be 'BE'")
        if len(tokens) != len(FIELD_NAMES) + 1:
            errors.append(
                f"expected {len(FIELD_NAMES)} numeric fields, got {max(0, len(tokens) - 1)}"
            )
        else:
            for name, token in zip(FIELD_NAMES, tokens[1:], strict=True):
                try:
                    values[name] = int(token, 10)
                except ValueError:
                    errors.append(f"field '{name}' is not a decimal integer")

    derived = _derive_values(values) if len(values) == len(FIELD_NAMES) else {}
    checksum_valid = (
        supplied_checksum is not None
        and calculated_checksum is not None
        and supplied_checksum == calculated_checksum
    )

    return {
        "received_at": received_at or utc_now_iso(),
        "raw_hex": bytes_to_hex(raw),
        "raw_ascii": bytes_to_ascii(raw),
        "byte_count": len(raw),
        "payload": payload_text,
        "supplied_checksum": supplied_checksum,
        "supplied_checksum_hex": (
            f"{supplied_checksum:02X}" if supplied_checksum is not None else None
        ),
        "calculated_checksum": calculated_checksum,
        "calculated_checksum_hex": (
            f"{calculated_checksum:02X}" if calculated_checksum is not None else None
        ),
        "checksum_valid": checksum_valid,
        "valid": not errors,
        "errors": errors,
        "values": values,
        "derived": derived,
    }


def build_frame(values: Mapping[str, int] | Iterable[int]) -> bytes:
    if isinstance(values, Mapping):
        numeric_values = [int(values[name]) for name in FIELD_NAMES]
    else:
        numeric_values = [int(value) for value in values]

    if len(numeric_values) != len(FIELD_NAMES):
        raise ValueError(f"expected {len(FIELD_NAMES)} values, got {len(numeric_values)}")

    payload = "BE," + ",".join(str(value) for value in numeric_values)
    payload_bytes = payload.encode("ascii")
    return b"$" + payload_bytes + f"*{calculate_xor(payload_bytes):02X}\r\n".encode("ascii")


@dataclass(slots=True)
class StreamEvent:
    kind: str
    raw: bytes
    reason: str | None = None
    frame: dict[str, Any] | None = None


class TelemetryStreamParser:
    """Recover CRLF-terminated telemetry frames from arbitrary serial chunks."""

    def __init__(self, *, max_frame_bytes: int = MAX_FRAME_BYTES) -> None:
        self.max_frame_bytes = max_frame_bytes
        self.buffer = bytearray()

    def reset(self) -> None:
        self.buffer.clear()

    def feed(self, chunk: bytes, *, received_at: str | None = None) -> list[StreamEvent]:
        if chunk:
            self.buffer.extend(chunk)

        events: list[StreamEvent] = []
        while self.buffer:
            start_index = self.buffer.find(START_MARKER)
            if start_index < 0:
                keep_count = self._partial_marker_suffix_length()
                noise_count = len(self.buffer) - keep_count
                if noise_count > 0:
                    noise = bytes(self.buffer[:noise_count])
                    del self.buffer[:noise_count]
                    events.append(StreamEvent("noise", noise, "bytes outside a telemetry frame"))
                break

            if start_index > 0:
                noise = bytes(self.buffer[:start_index])
                del self.buffer[:start_index]
                events.append(StreamEvent("noise", noise, "bytes before '$BE,' marker"))
                continue

            end_index = self.buffer.find(LINE_ENDING, len(START_MARKER))
            if end_index < 0:
                if len(self.buffer) > self.max_frame_bytes:
                    noise = bytes(self.buffer[:1])
                    del self.buffer[:1]
                    events.append(StreamEvent("noise", noise, "oversized unterminated frame"))
                    continue
                break

            frame_end = end_index + len(LINE_ENDING)
            raw_frame = bytes(self.buffer[:frame_end])
            del self.buffer[:frame_end]
            events.append(
                StreamEvent(
                    "frame",
                    raw_frame,
                    frame=parse_frame(raw_frame, received_at=received_at),
                )
            )

        return events

    def drain(self, *, reason: str = "stream closed with incomplete data") -> list[StreamEvent]:
        if not self.buffer:
            return []
        raw = bytes(self.buffer)
        self.buffer.clear()
        return [StreamEvent("noise", raw, reason)]

    def _partial_marker_suffix_length(self) -> int:
        max_length = min(len(self.buffer), len(START_MARKER) - 1)
        for length in range(max_length, 0, -1):
            if self.buffer[-length:] == START_MARKER[:length]:
                return length
        return 0

