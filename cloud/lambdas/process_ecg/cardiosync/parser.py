"""Parser for the binary session files written by the ESP32 firmware.

Layout (little-endian), see firmware/include/ecg_format.h:

    FileHeader (28 bytes)
    ECGSample  (3 x int16: I, II, III)   x num_ecg_samples
    IMUSample  (3 x int16: ax, ay, az)   x num_imu_samples
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

import numpy as np

HEADER_FORMAT = "<IHHIIHHII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)  # 28
SAMPLE_SIZE = 6  # 3 x int16

ECG_MAGIC = 0x45434744  # "ECGD"
ECG_SCALE_FACTOR = 6553.6  # int16 -> mV (±5 mV full scale)
ACCEL_SCALE = 16.0 / 32768.0  # int16 -> g (±16 g)

DEFAULT_ECG_FS = 250
DEFAULT_IMU_FS = 50


@dataclass
class Recording:
    header: dict
    ecg: np.ndarray  # (N, 3) in mV: leads I, II, III
    imu: np.ndarray  # (M, 3) in g, may be empty
    ecg_fs: int
    imu_fs: int

    @property
    def duration_s(self) -> float:
        return len(self.ecg) / self.ecg_fs


def parse_session(data: bytes) -> Recording:
    if len(data) < HEADER_SIZE:
        raise ValueError(f"File too small: {len(data)} bytes < {HEADER_SIZE}-byte header")

    fields = struct.unpack(HEADER_FORMAT, data[:HEADER_SIZE])
    header = dict(
        zip(
            (
                "magic",
                "version",
                "device_id",
                "session_id",
                "timestamp_start",
                "ecg_sample_rate",
                "imu_sample_rate",
                "num_ecg_samples",
                "num_imu_samples",
            ),
            fields,
            strict=True,
        )
    )

    if header["magic"] != ECG_MAGIC:
        raise ValueError(f"Invalid magic 0x{header['magic']:08X} (expected 0x{ECG_MAGIC:08X})")

    ecg_fs = header["ecg_sample_rate"] or DEFAULT_ECG_FS
    imu_fs = header["imu_sample_rate"] or DEFAULT_IMU_FS

    payload_samples = (len(data) - HEADER_SIZE) // SAMPLE_SIZE
    num_ecg = header["num_ecg_samples"]
    num_imu = header["num_imu_samples"]

    # A device reset during capture leaves the counters at 0: recover the
    # samples from the file size when the file holds only ECG data.
    if num_imu == 0 and (num_ecg == 0 or num_ecg > payload_samples):
        num_ecg = payload_samples

    if num_ecg == 0:
        raise ValueError("File contains no ECG samples")
    if num_ecg + num_imu > payload_samples:
        raise ValueError(
            f"Truncated file: header announces {num_ecg + num_imu} samples, payload holds {payload_samples}"
        )

    ecg_start = HEADER_SIZE
    ecg_end = ecg_start + num_ecg * SAMPLE_SIZE
    ecg_raw = np.frombuffer(data[ecg_start:ecg_end], dtype="<i2").reshape(-1, 3)
    ecg = ecg_raw.astype(np.float64) / ECG_SCALE_FACTOR

    if num_imu > 0:
        imu_end = ecg_end + num_imu * SAMPLE_SIZE
        imu_raw = np.frombuffer(data[ecg_end:imu_end], dtype="<i2").reshape(-1, 3)
        imu = imu_raw.astype(np.float64) * ACCEL_SCALE
    else:
        imu = np.zeros((0, 3), dtype=np.float64)

    header["num_ecg_samples"] = num_ecg
    return Recording(header=header, ecg=ecg, imu=imu, ecg_fs=ecg_fs, imu_fs=imu_fs)


def build_session(
    ecg_mv: np.ndarray,
    ecg_fs: int = DEFAULT_ECG_FS,
    session_id: int = 0,
    timestamp: int = 0,
) -> bytes:
    """Encode an (N, 3) ECG array in mV into the device file format (used by tests and tools)."""
    samples = np.clip(np.round(ecg_mv * ECG_SCALE_FACTOR), -32768, 32767).astype("<i2")
    header = struct.pack(HEADER_FORMAT, ECG_MAGIC, 1, 1, session_id, timestamp, ecg_fs, 0, len(samples), 0)
    return header + samples.tobytes()
