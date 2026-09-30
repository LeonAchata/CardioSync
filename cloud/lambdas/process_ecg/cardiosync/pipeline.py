"""End-to-end analysis of one session file, independent of AWS."""

from __future__ import annotations

import csv
import json
from datetime import UTC, datetime
from io import StringIO

import numpy as np

from .parser import parse_session
from .plots import render_plots
from .processing import LEAD_NAMES, SignalProcessor


def _signals_csv(t: np.ndarray, raw: np.ndarray, filtered: np.ndarray, motion: np.ndarray) -> str:
    out = StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(
        ["time_s"]
        + [f"ecg_{lead}_raw_mV" for lead in LEAD_NAMES]
        + [f"ecg_{lead}_filt_mV" for lead in LEAD_NAMES]
        + ["motion"]
    )
    for i in range(len(t)):
        writer.writerow(
            [f"{t[i]:.4f}"]
            + [f"{v:.4f}" for v in raw[i]]
            + [f"{v:.4f}" for v in filtered[i]]
            + [int(motion[i])]
        )
    return out.getvalue()


def analyze(
    data: bytes, source: str = "", with_plots: bool = True
) -> tuple[dict, dict[str, tuple[bytes, str]]]:
    """Processes a raw session file.

    Returns (metadata, files) where files maps an output name to (content, content_type).
    """
    rec = parse_session(data)
    processor = SignalProcessor(ecg_fs=rec.ecg_fs, imu_fs=rec.imu_fs)

    motion_imu = processor.detect_motion(rec.imu)
    filtered, _, heart_rates, motion_ecg = processor.process(rec.ecg, motion_imu)

    valid_bpm = [hr["bpm"] for hr in heart_rates.values() if hr["bpm"] > 0]
    avg_bpm = float(np.mean(valid_bpm)) if valid_bpm else 0.0
    motion_pct = float(motion_imu.mean() * 100) if len(motion_imu) else 0.0

    start = rec.header["timestamp_start"]
    metadata = {
        "source_file": source,
        "processed_at": datetime.now(UTC).isoformat(),
        "recorded_at": datetime.fromtimestamp(start, UTC).isoformat() if start else None,
        "session_id": rec.header["session_id"],
        "file_version": rec.header["version"],
        "duration_seconds": rec.duration_s,
        "ecg_samples": int(len(rec.ecg)),
        "imu_samples": int(len(rec.imu)),
        "ecg_sample_rate_hz": rec.ecg_fs,
        "imu_sample_rate_hz": rec.imu_fs if len(rec.imu) else 0,
        "motion_percentage": motion_pct,
        "heart_rate": {
            "average_bpm": avg_bpm,
            **{f"lead_{lead}": heart_rates[lead] for lead in LEAD_NAMES},
        },
    }

    t = np.arange(len(rec.ecg)) / rec.ecg_fs
    files: dict[str, tuple[bytes, str]] = {
        "metadata.json": (json.dumps(metadata, indent=2).encode(), "application/json"),
        "signals.csv": (_signals_csv(t, rec.ecg, filtered, motion_ecg).encode(), "text/csv"),
    }
    if with_plots:
        plots = render_plots(rec.ecg, filtered, rec.imu, motion_imu, heart_rates, rec.ecg_fs, rec.imu_fs)
        files.update({name: (png, "image/png") for name, png in plots.items()})

    return metadata, files
