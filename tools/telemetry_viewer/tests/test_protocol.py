from telemetry_viewer.protocol import (
    FIELD_NAMES,
    TelemetryStreamParser,
    build_frame,
    calculate_xor,
    parse_frame,
)


SAMPLE_VALUES = {
    "tick_ms": 685220,
    "pressure_online": 0,
    "pressure_status": 2,
    "pressure_read_mode": 0,
    "pressure_float_valid": 0,
    "pressure_raw": 0,
    "pressure_value_x1000": -2147483648,
    "pressure_unit_code": 0,
    "pressure_decimal_point": 0,
    "xda_online": 0,
    "xda_status": 2,
    "ec_x100": 0,
    "temperature_x10": 0,
    "tds_ppm": 0,
    "salinity_ppm": 0,
}


def test_known_screenshot_frame_has_expected_checksum_and_length() -> None:
    frame = build_frame(SAMPLE_VALUES)

    assert frame == (
        b"$BE,685220,0,2,0,0,0,-2147483648,0,0,0,2,0,0,0,0*38\r\n"
    )
    assert len(frame) == 53
    assert calculate_xor(frame[1 : frame.index(b"*")]) == 0x38


def test_parse_valid_frame_derives_sensor_values() -> None:
    values = dict(SAMPLE_VALUES)
    values.update(
        pressure_online=1,
        pressure_status=1,
        pressure_raw=1234,
        pressure_decimal_point=2,
        xda_online=1,
        xda_status=1,
        ec_x100=256,
        temperature_x10=253,
        tds_ppm=400,
        salinity_ppm=12,
    )

    parsed = parse_frame(build_frame(values))

    assert parsed["valid"] is True
    assert parsed["checksum_valid"] is True
    assert parsed["values"] == values
    assert parsed["derived"]["pressure_raw_scaled"] == 12.34
    assert parsed["derived"]["ec_value"] == 2.56
    assert parsed["derived"]["temperature_c"] == 25.3
    assert parsed["derived"]["pressure_status_text"] == "OK"


def test_stream_parser_reassembles_arbitrary_chunks_without_idle_timeout() -> None:
    frame = build_frame(SAMPLE_VALUES)
    parser = TelemetryStreamParser()

    events = []
    events.extend(parser.feed(frame[:11]))
    events.extend(parser.feed(frame[11:32]))
    events.extend(parser.feed(frame[32:]))

    assert [event.kind for event in events] == ["frame"]
    assert events[0].raw == frame
    assert events[0].frame is not None
    assert events[0].frame["valid"] is True


def test_stream_parser_separates_noise_from_valid_frame() -> None:
    noise = b"\x02\x03\x00\x00\x00\x04\x44\x3A\xFF\x00"
    frame = build_frame(SAMPLE_VALUES)
    parser = TelemetryStreamParser()

    events = parser.feed(noise + frame)

    assert [event.kind for event in events] == ["noise", "frame"]
    assert events[0].raw == noise
    assert events[1].frame is not None
    assert events[1].frame["checksum_valid"] is True


def test_stream_parser_keeps_partial_start_marker_between_reads() -> None:
    frame = build_frame(SAMPLE_VALUES)
    parser = TelemetryStreamParser()

    first = parser.feed(b"\x99\x88$B")
    second = parser.feed(frame[2:])

    assert len(first) == 1
    assert first[0].kind == "noise"
    assert first[0].raw == b"\x99\x88"
    assert len(second) == 1
    assert second[0].kind == "frame"
    assert second[0].raw == frame


def test_invalid_checksum_is_retained_as_invalid_frame() -> None:
    frame = bytearray(build_frame(SAMPLE_VALUES))
    frame[-4:-2] = b"00"

    parsed = parse_frame(bytes(frame))

    assert parsed["valid"] is False
    assert parsed["checksum_valid"] is False
    assert any("checksum mismatch" in error for error in parsed["errors"])
    assert set(parsed["values"]) == set(FIELD_NAMES)


def test_multiple_frames_can_arrive_in_one_chunk() -> None:
    parser = TelemetryStreamParser()
    first = build_frame(SAMPLE_VALUES)
    second_values = dict(SAMPLE_VALUES, tick_ms=690220)
    second = build_frame(second_values)

    events = parser.feed(first + second)

    assert [event.kind for event in events] == ["frame", "frame"]
    assert events[0].frame["values"]["tick_ms"] == 685220
    assert events[1].frame["values"]["tick_ms"] == 690220

