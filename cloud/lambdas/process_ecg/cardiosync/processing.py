"""ECG signal processing: band-pass + notch filtering, motion-adaptive wavelet
denoising and R-peak based heart-rate detection."""

from __future__ import annotations

import logging

import numpy as np
import pywt
from scipy import signal

logger = logging.getLogger(__name__)

LEAD_NAMES = ("I", "II", "III")


class SignalProcessor:
    def __init__(self, ecg_fs: int = 250, imu_fs: int = 50, mains_hz: float = 60.0):
        self.ecg_fs = ecg_fs
        self.imu_fs = imu_fs
        self.mains_hz = mains_hz

    # ------------------------------------------------------------------
    # Filters (zero-phase)
    # ------------------------------------------------------------------

    def notch_filter(self, x: np.ndarray, q: float = 30.0) -> np.ndarray:
        if self.mains_hz >= self.ecg_fs / 2:
            return x
        b, a = signal.iirnotch(self.mains_hz, q, self.ecg_fs)
        return signal.filtfilt(b, a, x)

    def highpass_filter(self, x: np.ndarray, cutoff: float, order: int = 4) -> np.ndarray:
        wn = cutoff / (0.5 * self.ecg_fs)
        if not 0 < wn < 1:
            return x
        sos = signal.butter(order, wn, btype="high", output="sos")
        return signal.sosfiltfilt(sos, x)

    def lowpass_filter(self, x: np.ndarray, cutoff: float, order: int = 4) -> np.ndarray:
        wn = min(cutoff / (0.5 * self.ecg_fs), 0.95)
        sos = signal.butter(order, wn, btype="low", output="sos")
        return signal.sosfiltfilt(sos, x)

    def preprocess_ecg(self, x: np.ndarray) -> np.ndarray:
        """0.5 Hz high-pass (baseline wander) -> low-pass (HF noise) -> mains notch."""
        nyquist = self.ecg_fs / 2
        y = self.highpass_filter(x, cutoff=0.5)
        y = self.lowpass_filter(y, cutoff=min(100.0, nyquist * 0.8))
        return self.notch_filter(y)

    @staticmethod
    def wavelet_denoise(
        x: np.ndarray, wavelet: str = "db4", level: int = 4, threshold_scale: float = 1.0
    ) -> np.ndarray:
        """Soft-threshold wavelet denoising with a universal (VisuShrink) threshold."""
        max_level = pywt.dwt_max_level(len(x), pywt.Wavelet(wavelet).dec_len)
        level = max(1, min(level, max_level))

        coeffs = pywt.wavedec(x, wavelet, level=level)
        sigma = np.median(np.abs(coeffs[-1])) / 0.6745
        threshold = threshold_scale * sigma * np.sqrt(2 * np.log(len(x)))

        denoised = [coeffs[0]] + [pywt.threshold(c, threshold, mode="soft") for c in coeffs[1:]]
        y = pywt.waverec(denoised, wavelet)
        return y[: len(x)] if len(y) >= len(x) else np.pad(y, (0, len(x) - len(y)))

    # ------------------------------------------------------------------
    # Motion
    # ------------------------------------------------------------------

    def detect_motion(self, accel: np.ndarray, window: int = 50, threshold_g: float = 0.3) -> np.ndarray:
        """Boolean mask (IMU rate) of samples where acceleration deviates from its trend."""
        if len(accel) == 0:
            return np.zeros(0, dtype=bool)
        magnitude = np.linalg.norm(accel, axis=1)
        if len(magnitude) >= window:
            trend = np.convolve(magnitude, np.ones(window) / window, mode="same")
        else:
            trend = magnitude
        return np.abs(magnitude - trend) > threshold_g

    @staticmethod
    def resample_mask(mask: np.ndarray, target_len: int) -> np.ndarray:
        if len(mask) == 0:
            return np.zeros(target_len, dtype=bool)
        if len(mask) == target_len:
            return mask
        idx = np.linspace(0, len(mask) - 1, target_len).astype(int)
        return mask[idx]

    # ------------------------------------------------------------------
    # Heart rate
    # ------------------------------------------------------------------

    def detect_heart_rate(self, x: np.ndarray) -> tuple[float, np.ndarray]:
        """Returns (bpm, r_peak_indices). Ignores 1 s at each edge (filter transients)."""
        fs = self.ecg_fs
        margin = int(fs) if len(x) > 4 * fs else 0
        segment = x[margin : len(x) - margin] if margin else x

        centered = segment - np.mean(segment)
        min_height = 3 * np.std(centered)
        min_distance = int(0.3 * fs)  # refractory period -> max 200 BPM

        peaks_pos, _ = signal.find_peaks(centered, height=min_height, distance=min_distance)
        peaks_neg, _ = signal.find_peaks(-centered, height=min_height, distance=min_distance)
        peaks = peaks_neg if len(peaks_neg) > len(peaks_pos) else peaks_pos  # inverted leads

        bpm = 60.0 / float(np.mean(np.diff(peaks) / fs)) if len(peaks) >= 2 else 0.0
        return bpm, peaks + margin

    # ------------------------------------------------------------------
    # Full pipeline
    # ------------------------------------------------------------------

    def process(self, ecg: np.ndarray, motion_mask_imu: np.ndarray, wavelet_level: int = 4):
        """Filters every lead and detects beats.

        Segments flagged as motion get a more aggressive wavelet threshold.
        Returns (filtered, preprocessed, heart_rates, motion_mask_ecg).
        """
        n_samples, n_leads = ecg.shape
        motion_mask = self.resample_mask(motion_mask_imu, n_samples)
        preprocessed = np.column_stack([self.preprocess_ecg(ecg[:, i]) for i in range(n_leads)])
        filtered = preprocessed.copy()
        heart_rates: dict[str, dict] = {}

        for i in range(n_leads):
            lead = LEAD_NAMES[i]
            for mask, scale in ((motion_mask, 2.0), (~motion_mask, 1.0)):
                idx = np.flatnonzero(mask)
                if len(idx) > 100:
                    filtered[idx, i] = self.wavelet_denoise(
                        preprocessed[idx, i], level=wavelet_level, threshold_scale=scale
                    )

            bpm, peaks = self.detect_heart_rate(filtered[:, i])
            heart_rates[lead] = {"bpm": bpm, "num_beats": int(len(peaks)), "r_peaks": peaks.tolist()}
            logger.info("Lead %s: %d beats, %.1f BPM", lead, len(peaks), bpm)

        return filtered, preprocessed, heart_rates, motion_mask
