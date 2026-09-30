"""PNG reports rendered with matplotlib (Agg backend, no display needed)."""

from __future__ import annotations

from io import BytesIO

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

from .processing import LEAD_NAMES  # noqa: E402


def _to_png(fig) -> bytes:
    buf = BytesIO()
    fig.savefig(buf, format="png", dpi=150)
    plt.close(fig)
    return buf.getvalue()


def _plot_lead(ax, t, y, peaks, color):
    ax.plot(t, y, color=color, linewidth=0.8)
    if len(peaks):
        ax.scatter(t[peaks], y[peaks], c="red", s=50, marker="x", linewidths=2, label="R peaks")
        ax.legend(loc="upper right", fontsize=9)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(0, t[-1])


def render_plots(
    ecg_raw: np.ndarray,
    ecg_filtered: np.ndarray,
    imu: np.ndarray,
    motion_mask_imu: np.ndarray,
    heart_rates: dict,
    ecg_fs: int,
    imu_fs: int,
) -> dict[str, bytes]:
    n = len(ecg_filtered)
    t = np.arange(n) / ecg_fs
    duration = n / ecg_fs
    peaks = {lead: np.asarray(hr["r_peaks"], dtype=int) for lead, hr in heart_rates.items()}
    bpm = {lead: hr["bpm"] for lead, hr in heart_rates.items()}
    plots: dict[str, bytes] = {}

    # 1. Filtered ECG, all leads
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    fig.suptitle(f"Filtered ECG - 3 leads ({duration:.1f} s)", fontsize=14, fontweight="bold")
    for i, ax in enumerate(axes):
        lead = LEAD_NAMES[i]
        _plot_lead(ax, t, ecg_filtered[:, i], peaks.get(lead, []), "darkblue")
        ax.set_ylabel(f"{lead} (mV)")
        ax.set_title(f"Lead {lead} - {bpm.get(lead, 0):.1f} BPM", fontsize=11)
    axes[-1].set_xlabel("Time (s)")
    fig.tight_layout()
    plots["ecg_filtered.png"] = _to_png(fig)

    # 2. Raw vs filtered, lead II
    fig, axes = plt.subplots(2, 1, figsize=(14, 8), sharex=True)
    fig.suptitle(
        f"Raw vs filtered ECG (lead II) - {bpm.get('II', 0):.1f} BPM", fontsize=14, fontweight="bold"
    )
    axes[0].plot(t, ecg_raw[:, 1], color="gray", linewidth=0.5)
    axes[0].set_title("Raw signal")
    axes[0].set_ylabel("Raw (mV)")
    axes[0].grid(True, alpha=0.3)
    _plot_lead(axes[1], t, ecg_filtered[:, 1], peaks.get("II", []), "darkgreen")
    axes[1].set_title("Filtered signal")
    axes[1].set_ylabel("Filtered (mV)")
    axes[1].set_xlabel("Time (s)")
    fig.tight_layout()
    plots["ecg_comparison.png"] = _to_png(fig)

    # 3. IMU (only when the device recorded accelerometer data)
    if len(imu) > 0:
        t_imu = np.arange(len(imu)) / imu_fs
        fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)
        fig.suptitle("Accelerometer and motion mask", fontsize=14, fontweight="bold")
        for i, (axis, color) in enumerate(zip("XYZ", ("red", "green", "blue"), strict=True)):
            axes[i].plot(t_imu, imu[:, i], color=color, linewidth=0.8)
            axes[i].set_ylabel(f"{axis} (g)")
            axes[i].grid(True, alpha=0.3)
        axes[3].fill_between(t_imu, 0, motion_mask_imu.astype(float), color="orange", alpha=0.5)
        axes[3].set_yticks([0, 1], ["Still", "Motion"])
        axes[3].set_xlabel("Time (s)")
        fig.tight_layout()
        plots["imu_accel.png"] = _to_png(fig)

    return plots
