from __future__ import annotations

import struct
import zlib
from pathlib import Path


def make_header(
    *,
    file_index: int = 0,
    start_tick_ms: int = 1000,
    sample_period_ms: int = 5000,
) -> bytes:
    data = bytearray(512)
    data[:8] = b"BEYELOG1"
    struct.pack_into("<HHHH", data, 8, 1, 512, 64, 3)
    struct.pack_into("<III", data, 16, sample_period_ms, file_index, start_tick_ms)
    struct.pack_into("<I", data, 508, zlib.crc32(data[:508]) & 0xFFFFFFFF)
    return bytes(data)


def make_record(
    *,
    sequence: int = 1,
    tick_ms: int = 6000,
    pressure_sample_tick: int = 5900,
    pressure_sample_sequence: int = 10,
    xda_sample_tick: int = 5800,
    xda_sample_sequence: int = 20,
    pressure_value: float = 12.5,
    pressure_raw: int = -1234,
    pressure_unit_code: int = 4,
    pressure_decimal_point: int = 2,
    ec_x100: int = 4567,
    temperature_x10: int = -125,
    tds_ppm: int = 321,
    salinity_ppm: int = 111,
    pressure_online: int = 1,
    pressure_status: int = 1,
    pressure_float_valid: int = 1,
    pressure_read_mode: int = 1,
    xda_online: int = 1,
    xda_status: int = 1,
    pressure_exception_code: int = 0,
    xda_exception_code: int = 0,
) -> bytes:
    data = bytearray(64)
    data[:4] = b"BECR"
    struct.pack_into("<HH", data, 4, 3, 64)
    struct.pack_into(
        "<IIIIII",
        data,
        8,
        sequence,
        tick_ms,
        pressure_sample_tick,
        pressure_sample_sequence,
        xda_sample_tick,
        xda_sample_sequence,
    )
    struct.pack_into("<f", data, 32, pressure_value)
    struct.pack_into("<hHHHhHH", data, 36, pressure_raw, pressure_unit_code, pressure_decimal_point, ec_x100, temperature_x10, tds_ppm, salinity_ppm)
    data[50:58] = bytes(
        [
            pressure_online,
            pressure_status,
            pressure_float_valid,
            pressure_read_mode,
            xda_online,
            xda_status,
            pressure_exception_code,
            xda_exception_code,
        ]
    )
    struct.pack_into("<I", data, 60, zlib.crc32(data[:60]) & 0xFFFFFFFF)
    return bytes(data)


def write_log(path: Path, records: list[bytes], *, file_index: int = 0, tail: bytes = b"") -> Path:
    path.write_bytes(make_header(file_index=file_index) + b"".join(records) + tail)
    return path
