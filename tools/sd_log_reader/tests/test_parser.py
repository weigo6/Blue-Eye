from __future__ import annotations

import struct

import pytest

from sd_log_reader.parser import DatasetLoader, crc32_iso_hdlc, decode_record_bytes, parse_header_bytes

from .helpers import make_header, make_record, write_log


def test_crc_matches_firmware_style() -> None:
    assert crc32_iso_hdlc(b"123456789") == 0xCBF43926


def test_parse_header_and_record_fields() -> None:
    header, issues = parse_header_bytes(make_header(file_index=42, start_tick_ms=123456))
    assert issues == []
    assert header is not None
    assert header.file_index == 42
    assert header.start_tick_ms == 123456
    assert header.crc_valid

    record, issue = decode_record_bytes(
        make_record(),
        source_file="LOG00042.BIN",
        file_index=42,
        record_index=0,
        global_index=0,
        file_offset=512,
    )
    assert issue is None
    assert record is not None
    assert record.pressure == pytest.approx(12.5)
    assert record.pressure_raw == -1234
    assert record.temperature == pytest.approx(-12.5)
    assert record.ec == pytest.approx(45.67)
    assert record.pressure_sample_age_ms == 100
    assert record.xda_sample_age_ms == 200


def test_raw_pressure_engineering_value() -> None:
    record, issue = decode_record_bytes(
        make_record(pressure_read_mode=0, pressure_float_valid=0, pressure_raw=-1234, pressure_decimal_point=2),
        source_file="LOG00000.BIN",
        file_index=0,
        record_index=0,
        global_index=0,
        file_offset=512,
    )
    assert issue is None
    assert record is not None
    assert record.pressure == pytest.approx(-12.34)


def test_header_crc_failure_is_fatal(tmp_path) -> None:
    header = bytearray(make_header())
    header[100] ^= 0x80
    path = tmp_path / "LOG00000.BIN"
    path.write_bytes(header + make_record())

    dataset = DatasetLoader().load(path)
    scan = dataset.files[0]
    assert not scan.usable
    assert scan.valid_records == 0
    assert scan.issues[0].code == "HEADER_CRC"


def test_bad_record_crc_stops_file_at_first_failure(tmp_path) -> None:
    corrupt = bytearray(make_record(sequence=3, tick_ms=16000))
    corrupt[42] ^= 0x01
    path = write_log(
        tmp_path / "LOG00000.BIN",
        [
            make_record(sequence=1, tick_ms=6000),
            make_record(sequence=2, tick_ms=11000),
            bytes(corrupt),
            make_record(sequence=4, tick_ms=21000),
        ],
    )

    dataset = DatasetLoader().load(path)
    scan = dataset.files[0]
    assert scan.usable
    assert scan.valid_records == 2
    assert scan.issues[0].code == "RECORD_CRC"
    assert scan.issues[0].record_index == 2
    assert [record.sequence for record in dataset.iter_records()] == [1, 2]


def test_trailing_bytes_and_sequence_gap_are_reported(tmp_path) -> None:
    path = write_log(
        tmp_path / "LOG00007.BIN",
        [make_record(sequence=10), make_record(sequence=13, tick_ms=11000)],
        file_index=7,
        tail=b"partial",
    )
    dataset = DatasetLoader().load(path)
    scan = dataset.files[0]
    assert scan.valid_records == 2
    assert scan.trailing_bytes == 7
    assert scan.sequence_gap_count == 1
    assert scan.missed_sequence_count == 2
    assert scan.issues[0].code == "RECORD_TRUNCATED"


def test_directory_discovery_sorts_files_and_pages_records(tmp_path) -> None:
    log_dir = tmp_path / "LOG"
    log_dir.mkdir()
    write_log(log_dir / "LOG00002.BIN", [make_record(sequence=3)], file_index=2)
    write_log(
        log_dir / "LOG00001.BIN",
        [make_record(sequence=1), make_record(sequence=2, tick_ms=11000)],
        file_index=1,
    )

    dataset = DatasetLoader(max_preview_points=100).load(tmp_path)
    assert [file.path.name for file in dataset.files] == ["LOG00001.BIN", "LOG00002.BIN"]
    assert dataset.total_records == 3
    assert [record.sequence for record in dataset.iter_records(offset=1, limit=2)] == [2, 3]
    assert dataset.preview[0].global_index == 0
    assert dataset.preview[-1].global_index == 2


def test_record_magic_failure(tmp_path) -> None:
    broken = bytearray(make_record())
    broken[:4] = b"NOPE"
    struct.pack_into("<I", broken, 60, crc32_iso_hdlc(broken[:60]))
    path = write_log(tmp_path / "LOG00000.BIN", [bytes(broken)])
    scan = DatasetLoader().load(path).files[0]
    assert scan.valid_records == 0
    assert scan.issues[0].code == "RECORD_MAGIC"
