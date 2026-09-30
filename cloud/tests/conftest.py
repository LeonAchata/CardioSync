import importlib.util
import os
import sys
from pathlib import Path

import numpy as np
import pytest

LAMBDAS = Path(__file__).resolve().parents[1] / "lambdas"
sys.path.insert(0, str(LAMBDAS / "process_ecg"))

# Offline defaults so boto3 clients can be built (and URLs presigned) without AWS
os.environ.setdefault("AWS_DEFAULT_REGION", "us-east-1")
os.environ.setdefault("AWS_ACCESS_KEY_ID", "testing")
os.environ.setdefault("AWS_SECRET_ACCESS_KEY", "testing")


def load_lambda(name: str):
    """Both lambdas expose app.py, so load each one under a unique module name."""
    spec = importlib.util.spec_from_file_location(f"{name}_app", LAMBDAS / name / "app.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def synthetic_ecg(bpm: float = 72.0, duration_s: float = 15.0, fs: int = 250, seed: int = 0):
    """3-lead ECG-like signal in mV: QRS + T waves, baseline wander, mains hum and noise."""
    rng = np.random.default_rng(seed)
    t = np.arange(int(duration_s * fs)) / fs
    beat = np.zeros_like(t)
    for r in np.arange(0.5, duration_s, 60.0 / bpm):
        beat += 1.0 * np.exp(-(((t - r) / 0.012) ** 2))  # R wave
        beat += 0.25 * np.exp(-(((t - r - 0.25) / 0.045) ** 2))  # T wave
    noise = (
        0.2 * np.sin(2 * np.pi * 0.3 * t)  # baseline wander
        + 0.05 * np.sin(2 * np.pi * 60 * t)  # mains interference
        + 0.02 * rng.standard_normal(len(t))
    )
    lead_ii = beat + noise
    lead_i = 0.6 * beat + noise
    return np.column_stack([lead_i, lead_ii, lead_ii - lead_i])


@pytest.fixture
def ecg_72bpm():
    return synthetic_ecg()
