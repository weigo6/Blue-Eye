from __future__ import annotations

import math
import re
import struct
import zlib
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import BinaryIO, Iterable, Iterator


FILE_MAGIC = b"BEYELOG1"
RECORD_MAGIC = b"BECR"
FILE_HEADER_SIZE = 512
RECORD_SIZE = 64
SUPPORTED_FILE_VERSION = 1
SUPPORTED_RECORD_VERSION = 3
MAX_PREVIEW_POINTS = 3000
LOG_FILE_PATTERN = re.compile(r"^LOG(?P<index>\d{5})\.BIN$", re.IGNORECASE)

STATUS_NAMES = {
    0: "IDLE",
    1: "OK",
    2: "TIMEOUT",
    3: "CRC_ERROR",
    4: "FRAME_ERROR",
    5: "UART_ERROR",
    6: "MODBUS_EXCEPTION",
}


def crc32_iso_hdlc(data: bytes | bytearray | memoryview) -> int:
    """Return the CRC used by the firmware's reflected CRC-32 implementation."""

    return zlib.crc32(data) & 0xFFFFFFFF


def unsigned_delta(current: int, previous: int) -> int:
    return (current - previous) & 0xFFFFFFFF


def pressure_engineering_value(
    read_mode: int,
    float_valid: int,
    pressure_value: float,
    pressure_raw: int,
    decimal_point: int,
) -> float | None:
    if read_mode == 1 and float_valid and math.isfinite(pressure_value):
        return pressure_value
    if read_mode == 0 and decimal_point <= 9:
        return pressure_raw / (10**decimal_point)
    return None


@dataclass(frozen=True, slots=True)
class FileHeader:
    magic: str
    file_version: int
    header_size: int
    record_size: int
    record_version: int
    sample_period_ms: int
    file_index: int
    start_tick_ms: int
    crc32_stored: int
    crc32_calculated: int

    @property
    def crc_valid(self) -> bool:
        return self.crc32_stored == self.crc32_calculated

    def to_dict(self) -> dict[str, object]:
        result = asdict(self)
        result["crc_valid"] = self.crc_valid
        return result


@dataclass(frozen=True, slots=True)
class ParseIssue:
    code: str
    message: str
    offset: int | None = None
    record_index: int | None = None

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True, slots=True)
class SensorRecord:
    source_file: str
    file_index: int
    record_index: int
    global_index: int
    file_offset: int
    sequence: int
    tick_ms: int
    pressure_sample_tick: int
    pressure_sample_sequence: int
    xda_sample_tick: int
    xda_sample_sequence: int
    pressure_value: float
    pressure_raw: int
    pressure_unit_code: int
    pressure_decimal_point: int
    ec_x100: int
    temperature_x10: int
    tds_ppm: int
    salinity_ppm: int
    pressure_online: int
    pressure_status: int
    pressure_float_valid: int
    pressure_read_mode: int
    xda_online: int
    xda_status: int
    pressure_exception_code: int
    xda_exception_code: int
    crc32_stored: int
    crc32_calculated: int

    @property
    def pressure(self) -> float | None:
        return pressure_engineering_value(
            self.pressure_read_mode,
            self.pressure_float_valid,
            self.pressure_value,
            self.pressure_raw,
            self.pressure_decimal_point,
        )

    @property
    def ec(self) -> float:
        return self.ec_x100 / 100.0

    @property
    def temperature(self) -> float:
        return self.temperature_x10 / 10.0

    @property
    def pressure_status_name(self) -> str:
        return STATUS_NAMES.get(self.pressure_status, f"UNKNOWN_{self.pressure_status}")

    @property
    def xda_status_name(self) -> str:
        return STATUS_NAMES.get(self.xda_status, f"UNKNOWN_{self.xda_status}")

    @property
    def pressure_sample_age_ms(self) -> int | None:
        if self.pressure_sample_tick == 0:
            return None
        return unsigned_delta(self.tick_ms, self.pressure_sample_tick)

    @property
    def xda_sample_age_ms(self) -> int | None:
        if self.xda_sample_tick == 0:
            return None
        return unsigned_delta(self.tick_ms, self.xda_sample_tick)

    def to_dict(self) -> dict[str, object]:
        return {
            "source_file": self.source_file,
            "file_index": self.file_index,
            "record_index": self.record_index,
            "global_index": self.global_index,
            "file_offset": self.file_offset,
            "sequence": self.sequence,
            "tick_ms": self.tick_ms,
            "pressure_sample_tick": self.pressure_sample_tick,
            "pressure_sample_sequence": self.pressure_sample_sequence,
            "xda_sample_tick": self.xda_sample_tick,
            "xda_sample_sequence": self.xda_sample_sequence,
            "pressure": self.pressure,
            "pressure_value": self.pressure_value if math.isfinite(self.pressure_value) else None,
            "pressure_raw": self.pressure_raw,
            "pressure_unit_code": self.pressure_unit_code,
            "pressure_decimal_point": self.pressure_decimal_point,
            "ec": self.ec,
            "ec_x100": self.ec_x100,
            "temperature": self.temperature,
            "temperature_x10": self.temperature_x10,
            "tds_ppm": self.tds_ppm,
            "salinity_ppm": self.salinity_ppm,
            "pressure_online": bool(self.pressure_online),
            "pressure_status": self.pressure_status,
            "pressure_status_name": self.pressure_status_name,
            "pressure_float_valid": bool(self.pressure_float_valid),
            "pressure_read_mode": self.pressure_read_mode,
            "pressure_read_mode_name": "FLOAT" if self.pressure_read_mode == 1 else "RAW",
            "pressure_sample_age_ms": self.pressure_sample_age_ms,
            "xda_online": bool(self.xda_online),
            "xda_status": self.xda_status,
            "xda_status_name": self.xda_status_name,
            "xda_sample_age_ms": self.xda_sample_age_ms,
            "pressure_exception_code": self.pressure_exception_code,
            "xda_exception_code": self.xda_exception_code,
            "crc32_stored": self.crc32_stored,
            "crc32_calculated": self.crc32_calculated,
        }


@dataclass(slots=True)
class MetricStats:
    count: int = 0
    total: float = 0.0
    minimum: float | None = None
    maximum: float | None = None

    def add(self, value: float | None) -> None:
        if value is None or not math.isfinite(value):
            return
        self.count += 1
        self.total += value
        self.minimum = value if self.minimum is None else min(self.minimum, value)
        self.maximum = value if self.maximum is None else max(self.maximum, value)

    def merge(self, other: MetricStats) -> None:
        if other.count == 0:
            return
        self.count += other.count
        self.total += other.total
        if other.minimum is not None:
            self.minimum = other.minimum if self.minimum is None else min(self.minimum, other.minimum)
        if other.maximum is not None:
            self.maximum = other.maximum if self.maximum is None else max(self.maximum, other.maximum)

    def to_dict(self) -> dict[str, float | int | None]:
        return {
            "count": self.count,
            "min": self.minimum,
            "max": self.maximum,
            "avg": (self.total / self.count) if self.count else None,
        }


@dataclass(slots=True)
class ScanStats:
    pressure: MetricStats = field(default_factory=MetricStats)
    ec: MetricStats = field(default_factory=MetricStats)
    temperature: MetricStats = field(default_factory=MetricStats)
    tds: MetricStats = field(default_factory=MetricStats)
    salinity: MetricStats = field(default_factory=MetricStats)
    pressure_offline_count: int = 0
    xda_offline_count: int = 0
    pressure_error_count: int = 0
    xda_error_count: int = 0
    pressure_status_counts: dict[str, int] = field(default_factory=dict)
    xda_status_counts: dict[str, int] = field(default_factory=dict)

    def add_record(self, record: SensorRecord) -> None:
        self.pressure.add(record.pressure)
        self.ec.add(record.ec)
        self.temperature.add(record.temperature)
        self.tds.add(float(record.tds_ppm))
        self.salinity.add(float(record.salinity_ppm))
        if not record.pressure_online:
            self.pressure_offline_count += 1
        if not record.xda_online:
            self.xda_offline_count += 1
        if record.pressure_status not in (0, 1):
            self.pressure_error_count += 1
        if record.xda_status not in (0, 1):
            self.xda_error_count += 1
        self.pressure_status_counts[record.pressure_status_name] = (
            self.pressure_status_counts.get(record.pressure_status_name, 0) + 1
        )
        self.xda_status_counts[record.xda_status_name] = (
            self.xda_status_counts.get(record.xda_status_name, 0) + 1
        )

    def merge(self, other: ScanStats) -> None:
        for name in ("pressure", "ec", "temperature", "tds", "salinity"):
            getattr(self, name).merge(getattr(other, name))
        self.pressure_offline_count += other.pressure_offline_count
        self.xda_offline_count += other.xda_offline_count
        self.pressure_error_count += other.pressure_error_count
        self.xda_error_count += other.xda_error_count
        for key, value in other.pressure_status_counts.items():
            self.pressure_status_counts[key] = self.pressure_status_counts.get(key, 0) + value
        for key, value in other.xda_status_counts.items():
            self.xda_status_counts[key] = self.xda_status_counts.get(key, 0) + value

    def to_dict(self) -> dict[str, object]:
        return {
            "metrics": {
                "pressure": self.pressure.to_dict(),
                "ec": self.ec.to_dict(),
                "temperature": self.temperature.to_dict(),
                "tds": self.tds.to_dict(),
                "salinity": self.salinity.to_dict(),
            },
            "pressure_offline_count": self.pressure_offline_count,
            "xda_offline_count": self.xda_offline_count,
            "pressure_error_count": self.pressure_error_count,
            "xda_error_count": self.xda_error_count,
            "pressure_status_counts": dict(sorted(self.pressure_status_counts.items())),
            "xda_status_counts": dict(sorted(self.xda_status_counts.items())),
        }


@dataclass(slots=True)
class LogFileScan:
    path: Path
    size_bytes: int
    header: FileHeader | None = None
    valid_records: int = 0
    possible_records: int = 0
    trailing_bytes: int = 0
    global_start_index: int = 0
    first_sequence: int | None = None
    last_sequence: int | None = None
    first_tick_ms: int | None = None
    last_tick_ms: int | None = None
    sequence_gap_count: int = 0
    missed_sequence_count: int = 0
    out_of_order_count: int = 0
    issues: list[ParseIssue] = field(default_factory=list)
    stats: ScanStats = field(default_factory=ScanStats)

    @property
    def usable(self) -> bool:
        return self.header is not None and self.header.crc_valid and not any(
            issue.code.startswith("HEADER_") for issue in self.issues
        )

    @property
    def clean(self) -> bool:
        return self.usable and not self.issues

    @property
    def duration_ms(self) -> int | None:
        if self.first_tick_ms is None or self.last_tick_ms is None:
            return None
        return unsigned_delta(self.last_tick_ms, self.first_tick_ms)

    def to_dict(self) -> dict[str, object]:
        return {
            "path": str(self.path),
            "name": self.path.name,
            "size_bytes": self.size_bytes,
            "header": self.header.to_dict() if self.header else None,
            "valid_records": self.valid_records,
            "possible_records": self.possible_records,
            "trailing_bytes": self.trailing_bytes,
            "global_start_index": self.global_start_index,
            "first_sequence": self.first_sequence,
            "last_sequence": self.last_sequence,
            "first_tick_ms": self.first_tick_ms,
            "last_tick_ms": self.last_tick_ms,
            "duration_ms": self.duration_ms,
            "sequence_gap_count": self.sequence_gap_count,
            "missed_sequence_count": self.missed_sequence_count,
            "out_of_order_count": self.out_of_order_count,
            "usable": self.usable,
            "clean": self.clean,
            "issues": [issue.to_dict() for issue in self.issues],
            "stats": self.stats.to_dict(),
        }


@dataclass(frozen=True, slots=True)
class PreviewPoint:
    global_index: int
    source_file: str
    sequence: int
    tick_ms: int
    pressure: float | None
    ec: float
    temperature: float
    tds_ppm: int
    salinity_ppm: int
    pressure_online: bool
    xda_online: bool

    @classmethod
    def from_record(cls, record: SensorRecord) -> PreviewPoint:
        return cls(
            global_index=record.global_index,
            source_file=record.source_file,
            sequence=record.sequence,
            tick_ms=record.tick_ms,
            pressure=record.pressure,
            ec=record.ec,
            temperature=record.temperature,
            tds_ppm=record.tds_ppm,
            salinity_ppm=record.salinity_ppm,
            pressure_online=bool(record.pressure_online),
            xda_online=bool(record.xda_online),
        )

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(slots=True)
class Dataset:
    source: Path
    files: list[LogFileScan]
    preview: list[PreviewPoint]
    loaded_at: str
    stats: ScanStats

    @property
    def total_records(self) -> int:
        return sum(file.valid_records for file in self.files)

    @property
    def total_bytes(self) -> int:
        return sum(file.size_bytes for file in self.files)

    @property
    def issue_count(self) -> int:
        return sum(len(file.issues) for file in self.files)

    @property
    def problematic_file_count(self) -> int:
        return sum(1 for file in self.files if not file.clean)

    def summary_dict(self) -> dict[str, object]:
        return {
            "source": str(self.source),
            "loaded_at": self.loaded_at,
            "file_count": len(self.files),
            "usable_file_count": sum(1 for file in self.files if file.usable),
            "clean_file_count": sum(1 for file in self.files if file.clean),
            "problematic_file_count": self.problematic_file_count,
            "issue_count": self.issue_count,
            "total_bytes": self.total_bytes,
            "total_records": self.total_records,
            "sequence_gap_count": sum(file.sequence_gap_count for file in self.files),
            "missed_sequence_count": sum(file.missed_sequence_count for file in self.files),
            "out_of_order_count": sum(file.out_of_order_count for file in self.files),
            "stats": self.stats.to_dict(),
        }

    def to_dict(self, include_preview: bool = True) -> dict[str, object]:
        result = {
            "loaded": True,
            "summary": self.summary_dict(),
            "files": [file.to_dict() for file in self.files],
        }
        if include_preview:
            result["preview"] = [point.to_dict() for point in self.preview]
        return result

    def iter_records(self, offset: int = 0, limit: int | None = None) -> Iterator[SensorRecord]:
        if offset < 0:
            raise ValueError("offset 不能小于 0")
        if limit is not None and limit < 0:
            raise ValueError("limit 不能小于 0")
        if limit == 0 or offset >= self.total_records:
            return

        remaining_skip = offset
        remaining_limit = limit
        for scan in self.files:
            if not scan.usable or scan.valid_records == 0:
                continue
            if remaining_skip >= scan.valid_records:
                remaining_skip -= scan.valid_records
                continue

            start_record = remaining_skip
            available = scan.valid_records - start_record
            take = available if remaining_limit is None else min(available, remaining_limit)
            yield from iter_file_records(
                scan,
                start_record=start_record,
                count=take,
            )
            remaining_skip = 0
            if remaining_limit is not None:
                remaining_limit -= take
                if remaining_limit <= 0:
                    break


def parse_header_bytes(data: bytes) -> tuple[FileHeader | None, list[ParseIssue]]:
    issues: list[ParseIssue] = []
    if len(data) < FILE_HEADER_SIZE:
        issues.append(
            ParseIssue(
                "HEADER_TRUNCATED",
                f"文件头不足 {FILE_HEADER_SIZE} 字节，实际为 {len(data)} 字节",
                offset=0,
            )
        )
        return None, issues

    file_version, header_size, record_size, record_version = struct.unpack_from("<HHHH", data, 8)
    sample_period_ms, file_index, start_tick_ms = struct.unpack_from("<III", data, 16)
    stored_crc = struct.unpack_from("<I", data, 508)[0]
    calculated_crc = crc32_iso_hdlc(data[:508])
    header = FileHeader(
        magic=data[:8].decode("ascii", errors="replace"),
        file_version=file_version,
        header_size=header_size,
        record_size=record_size,
        record_version=record_version,
        sample_period_ms=sample_period_ms,
        file_index=file_index,
        start_tick_ms=start_tick_ms,
        crc32_stored=stored_crc,
        crc32_calculated=calculated_crc,
    )

    if data[:8] != FILE_MAGIC:
        issues.append(ParseIssue("HEADER_MAGIC", "文件头魔数不是 BEYELOG1", offset=0))
    if file_version != SUPPORTED_FILE_VERSION:
        issues.append(
            ParseIssue(
                "HEADER_VERSION",
                f"不支持的文件格式版本 {file_version}，当前仅支持 {SUPPORTED_FILE_VERSION}",
                offset=8,
            )
        )
    if header_size != FILE_HEADER_SIZE:
        issues.append(
            ParseIssue(
                "HEADER_SIZE",
                f"文件头声明长度为 {header_size}，预期为 {FILE_HEADER_SIZE}",
                offset=10,
            )
        )
    if record_size != RECORD_SIZE:
        issues.append(
            ParseIssue(
                "HEADER_RECORD_SIZE",
                f"记录声明长度为 {record_size}，预期为 {RECORD_SIZE}",
                offset=12,
            )
        )
    if record_version != SUPPORTED_RECORD_VERSION:
        issues.append(
            ParseIssue(
                "HEADER_RECORD_VERSION",
                f"不支持的记录版本 {record_version}，当前仅支持 {SUPPORTED_RECORD_VERSION}",
                offset=14,
            )
        )
    if not header.crc_valid:
        issues.append(
            ParseIssue(
                "HEADER_CRC",
                f"文件头 CRC 错误：存储 0x{stored_crc:08X}，计算 0x{calculated_crc:08X}",
                offset=508,
            )
        )
    return header, issues


def decode_record_bytes(
    data: bytes,
    *,
    source_file: str,
    file_index: int,
    record_index: int,
    global_index: int,
    file_offset: int,
) -> tuple[SensorRecord | None, ParseIssue | None]:
    if len(data) != RECORD_SIZE:
        return None, ParseIssue(
            "RECORD_TRUNCATED",
            f"记录不足 {RECORD_SIZE} 字节，实际为 {len(data)} 字节",
            offset=file_offset,
            record_index=record_index,
        )
    if data[:4] != RECORD_MAGIC:
        return None, ParseIssue(
            "RECORD_MAGIC",
            f"记录魔数错误：{data[:4].hex(' ').upper()}",
            offset=file_offset,
            record_index=record_index,
        )

    version, record_size = struct.unpack_from("<HH", data, 4)
    if version != SUPPORTED_RECORD_VERSION:
        return None, ParseIssue(
            "RECORD_VERSION",
            f"记录版本 {version} 不受支持",
            offset=file_offset + 4,
            record_index=record_index,
        )
    if record_size != RECORD_SIZE:
        return None, ParseIssue(
            "RECORD_SIZE",
            f"记录声明长度为 {record_size}，预期为 {RECORD_SIZE}",
            offset=file_offset + 6,
            record_index=record_index,
        )

    stored_crc = struct.unpack_from("<I", data, 60)[0]
    calculated_crc = crc32_iso_hdlc(data[:60])
    if stored_crc != calculated_crc:
        return None, ParseIssue(
            "RECORD_CRC",
            f"记录 CRC 错误：存储 0x{stored_crc:08X}，计算 0x{calculated_crc:08X}",
            offset=file_offset + 60,
            record_index=record_index,
        )

    sequence, tick_ms, pressure_tick, pressure_sequence, xda_tick, xda_sequence = (
        struct.unpack_from("<IIIIII", data, 8)
    )
    pressure_value = struct.unpack_from("<f", data, 32)[0]
    pressure_raw = struct.unpack_from("<h", data, 36)[0]
    pressure_unit_code, decimal_point, ec_x100 = struct.unpack_from("<HHH", data, 38)
    temperature_x10 = struct.unpack_from("<h", data, 44)[0]
    tds_ppm, salinity_ppm = struct.unpack_from("<HH", data, 46)

    return (
        SensorRecord(
            source_file=source_file,
            file_index=file_index,
            record_index=record_index,
            global_index=global_index,
            file_offset=file_offset,
            sequence=sequence,
            tick_ms=tick_ms,
            pressure_sample_tick=pressure_tick,
            pressure_sample_sequence=pressure_sequence,
            xda_sample_tick=xda_tick,
            xda_sample_sequence=xda_sequence,
            pressure_value=pressure_value,
            pressure_raw=pressure_raw,
            pressure_unit_code=pressure_unit_code,
            pressure_decimal_point=decimal_point,
            ec_x100=ec_x100,
            temperature_x10=temperature_x10,
            tds_ppm=tds_ppm,
            salinity_ppm=salinity_ppm,
            pressure_online=data[50],
            pressure_status=data[51],
            pressure_float_valid=data[52],
            pressure_read_mode=data[53],
            xda_online=data[54],
            xda_status=data[55],
            pressure_exception_code=data[56],
            xda_exception_code=data[57],
            crc32_stored=stored_crc,
            crc32_calculated=calculated_crc,
        ),
        None,
    )


def _read_header(stream: BinaryIO) -> tuple[FileHeader | None, list[ParseIssue]]:
    stream.seek(0)
    return parse_header_bytes(stream.read(FILE_HEADER_SIZE))


def scan_log_file(
    path: Path,
    *,
    global_start_index: int,
    preview_stride: int,
) -> tuple[LogFileScan, list[PreviewPoint]]:
    size_bytes = path.stat().st_size
    scan = LogFileScan(
        path=path,
        size_bytes=size_bytes,
        global_start_index=global_start_index,
    )
    preview: list[PreviewPoint] = []

    with path.open("rb") as stream:
        header, header_issues = _read_header(stream)
        scan.header = header
        scan.issues.extend(header_issues)
        if header is None or header_issues:
            return scan, preview

        payload_size = max(0, size_bytes - header.header_size)
        scan.possible_records, scan.trailing_bytes = divmod(payload_size, header.record_size)
        stream.seek(header.header_size)
        previous_sequence: int | None = None

        for record_index in range(scan.possible_records):
            file_offset = header.header_size + record_index * header.record_size
            data = stream.read(header.record_size)
            record, issue = decode_record_bytes(
                data,
                source_file=path.name,
                file_index=header.file_index,
                record_index=record_index,
                global_index=global_start_index + record_index,
                file_offset=file_offset,
            )
            if issue is not None:
                scan.issues.append(issue)
                break
            assert record is not None

            if scan.first_sequence is None:
                scan.first_sequence = record.sequence
                scan.first_tick_ms = record.tick_ms
            if previous_sequence is not None:
                sequence_delta = unsigned_delta(record.sequence, previous_sequence)
                if sequence_delta > 1 and sequence_delta < 0x80000000:
                    scan.sequence_gap_count += 1
                    scan.missed_sequence_count += sequence_delta - 1
                elif sequence_delta == 0 or sequence_delta >= 0x80000000:
                    scan.out_of_order_count += 1
            previous_sequence = record.sequence
            scan.last_sequence = record.sequence
            scan.last_tick_ms = record.tick_ms
            scan.valid_records += 1
            scan.stats.add_record(record)

            if record.global_index % preview_stride == 0:
                preview.append(PreviewPoint.from_record(record))

        if scan.trailing_bytes:
            scan.issues.append(
                ParseIssue(
                    "RECORD_TRUNCATED",
                    f"文件尾部剩余 {scan.trailing_bytes} 字节，不足一条完整记录",
                    offset=header.header_size + scan.possible_records * header.record_size,
                    record_index=scan.possible_records,
                )
            )

    return scan, preview


def iter_file_records(
    scan: LogFileScan,
    *,
    start_record: int = 0,
    count: int | None = None,
) -> Iterator[SensorRecord]:
    if not scan.usable or scan.header is None:
        return
    if start_record < 0 or start_record > scan.valid_records:
        raise ValueError("start_record 超出有效记录范围")
    available = scan.valid_records - start_record
    records_to_read = available if count is None else min(max(count, 0), available)
    with scan.path.open("rb") as stream:
        stream.seek(scan.header.header_size + start_record * scan.header.record_size)
        for relative_index in range(records_to_read):
            record_index = start_record + relative_index
            file_offset = scan.header.header_size + record_index * scan.header.record_size
            record, issue = decode_record_bytes(
                stream.read(scan.header.record_size),
                source_file=scan.path.name,
                file_index=scan.header.file_index,
                record_index=record_index,
                global_index=scan.global_start_index + record_index,
                file_offset=file_offset,
            )
            if issue is not None or record is None:
                raise OSError(f"已扫描的日志在偏移 {file_offset} 处发生变化")
            yield record


def discover_log_files(source: Path) -> list[Path]:
    source = source.expanduser().resolve()
    if not source.exists():
        raise FileNotFoundError(f"路径不存在：{source}")
    if source.is_file():
        return [source]
    if not source.is_dir():
        raise ValueError(f"不是普通文件或目录：{source}")

    search_dir = source / "LOG"
    if not search_dir.is_dir():
        search_dir = source

    candidates = [
        item
        for item in search_dir.iterdir()
        if item.is_file() and LOG_FILE_PATTERN.fullmatch(item.name)
    ]
    candidates.sort(key=_log_sort_key)
    if not candidates:
        raise FileNotFoundError(
            f"在 {search_dir} 中没有找到 LOG00000.BIN 形式的日志文件"
        )
    return candidates


def _log_sort_key(path: Path) -> tuple[int, str]:
    match = LOG_FILE_PATTERN.fullmatch(path.name)
    return (int(match.group("index")) if match else 2**31, path.name.upper())


class DatasetLoader:
    def __init__(self, max_preview_points: int = MAX_PREVIEW_POINTS) -> None:
        self.max_preview_points = max(100, max_preview_points)

    def load(self, source: str | Path) -> Dataset:
        source_path = Path(source).expanduser().resolve()
        paths = discover_log_files(source_path)
        possible_total = sum(
            max(0, path.stat().st_size - FILE_HEADER_SIZE) // RECORD_SIZE for path in paths
        )
        preview_stride = max(1, math.ceil(possible_total / self.max_preview_points))

        files: list[LogFileScan] = []
        preview: list[PreviewPoint] = []
        stats = ScanStats()
        global_start_index = 0
        for path in paths:
            scan, file_preview = scan_log_file(
                path,
                global_start_index=global_start_index,
                preview_stride=preview_stride,
            )
            files.append(scan)
            preview.extend(file_preview)
            stats.merge(scan.stats)
            global_start_index += scan.valid_records

        last_valid_file = next((file for file in reversed(files) if file.valid_records), None)
        if last_valid_file is not None:
            last_record = next(
                iter_file_records(
                    last_valid_file,
                    start_record=last_valid_file.valid_records - 1,
                    count=1,
                )
            )
            if not preview or preview[-1].global_index != last_record.global_index:
                preview.append(PreviewPoint.from_record(last_record))

        return Dataset(
            source=source_path,
            files=files,
            preview=preview,
            loaded_at=datetime.now(timezone.utc).isoformat(),
            stats=stats,
        )


CSV_COLUMNS = [
    "source_file",
    "file_index",
    "record_index",
    "global_index",
    "file_offset",
    "sequence",
    "tick_ms",
    "pressure_sample_tick",
    "pressure_sample_sequence",
    "pressure_sample_age_ms",
    "xda_sample_tick",
    "xda_sample_sequence",
    "xda_sample_age_ms",
    "pressure",
    "pressure_value",
    "pressure_raw",
    "pressure_unit_code",
    "pressure_decimal_point",
    "pressure_online",
    "pressure_status",
    "pressure_status_name",
    "pressure_float_valid",
    "pressure_read_mode",
    "pressure_read_mode_name",
    "ec",
    "ec_x100",
    "temperature",
    "temperature_x10",
    "tds_ppm",
    "salinity_ppm",
    "xda_online",
    "xda_status",
    "xda_status_name",
    "pressure_exception_code",
    "xda_exception_code",
    "crc32_stored",
    "crc32_calculated",
]


def records_as_dicts(records: Iterable[SensorRecord]) -> Iterator[dict[str, object]]:
    for record in records:
        yield record.to_dict()
