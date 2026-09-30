import io
import json
import struct

import numpy as np
import pytest
from cardiosync import analyze, parse_session
from cardiosync.parser import ECG_SCALE_FACTOR, HEADER_FORMAT, HEADER_SIZE, build_session
from conftest import load_lambda


def test_roundtrip_preserves_signal(ecg_72bpm):
    rec = parse_session(build_session(ecg_72bpm, session_id=7, timestamp=1727712000))

    assert rec.ecg.shape == ecg_72bpm.shape
    assert rec.ecg_fs == 250
    assert rec.header["session_id"] == 7
    assert np.max(np.abs(rec.ecg - ecg_72bpm)) <= 0.5 / ECG_SCALE_FACTOR + 1e-9
    assert rec.duration_s == pytest.approx(15.0)


def test_recovers_sample_count_when_header_was_not_patched(ecg_72bpm):
    data = bytearray(build_session(ecg_72bpm))
    struct.pack_into("<I", data, 20, 0)  # num_ecg_samples = 0 (device reset mid-capture)

    assert len(parse_session(bytes(data)).ecg) == len(ecg_72bpm)


def test_rejects_invalid_files(ecg_72bpm):
    with pytest.raises(ValueError, match="too small"):
        parse_session(b"\x00" * 10)

    bad_magic = bytearray(build_session(ecg_72bpm))
    bad_magic[0] ^= 0xFF
    with pytest.raises(ValueError, match="magic"):
        parse_session(bytes(bad_magic))

    empty = struct.pack(HEADER_FORMAT, 0x45434744, 1, 1, 0, 0, 250, 0, 0, 0)
    assert len(empty) == HEADER_SIZE
    with pytest.raises(ValueError, match="no ECG"):
        parse_session(empty)


@pytest.mark.parametrize("bpm", [55, 72, 110])
def test_heart_rate_detection(bpm):
    from conftest import synthetic_ecg

    metadata, _ = analyze(build_session(synthetic_ecg(bpm=bpm)), with_plots=False)

    assert metadata["heart_rate"]["lead_II"]["bpm"] == pytest.approx(bpm, abs=2)
    assert metadata["heart_rate"]["average_bpm"] == pytest.approx(bpm, abs=3)


def test_analyze_outputs(ecg_72bpm):
    metadata, files = analyze(build_session(ecg_72bpm, timestamp=1727712000), source="x.bin")

    assert set(files) == {"metadata.json", "signals.csv", "ecg_filtered.png", "ecg_comparison.png"}
    assert files["ecg_filtered.png"][0].startswith(b"\x89PNG")
    assert json.loads(files["metadata.json"][0])["recorded_at"].startswith("2024-09-30")

    csv_lines = files["signals.csv"][0].decode().splitlines()
    assert csv_lines[0].startswith("time_s,ecg_I_raw_mV")
    assert len(csv_lines) == len(ecg_72bpm) + 1
    assert metadata["ecg_samples"] == len(ecg_72bpm)


def test_output_prefix():
    app = load_lambda("process_ecg")
    assert app.output_prefix_for("raw/dev-1/session_1.bin") == "processed/dev-1/session_1/"


def test_lambda_handler_writes_results(monkeypatch, ecg_72bpm):
    app = load_lambda("process_ecg")
    monkeypatch.setenv("OUTPUT_BUCKET", "out-bucket")
    written = {}

    class FakeS3:
        def get_object(self, Bucket, Key):
            assert (Bucket, Key) == ("raw-bucket", "raw/dev 1/session_1.bin")
            return {"Body": io.BytesIO(build_session(ecg_72bpm))}

        def put_object(self, Bucket, Key, Body, ContentType):
            written[Key] = (Bucket, ContentType)

    monkeypatch.setattr(app, "s3", FakeS3())
    event = {
        "Records": [{"s3": {"bucket": {"name": "raw-bucket"}, "object": {"key": "raw/dev+1/session_1.bin"}}}]
    }

    result = app.lambda_handler(event, None)

    assert result["processed"][0]["average_bpm"] == pytest.approx(72, abs=3)
    assert written["processed/dev 1/session_1/metadata.json"] == ("out-bucket", "application/json")
    assert len(written) == 4
