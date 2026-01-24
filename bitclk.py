#!/usr/bin/env python3
"""
BitClk - Python replication of C bit clock recovery algorithm.
Visualizes PLL sampling instants and lock state on soft bit waveform.
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile

# Configuration
NUM_SAMPLES = 22050  # Number of samples to process
SAMPLE_RATE = 22050  # Sample rate (fallback if not in WAV)
BIT_RATE = 1200  # Bit rate (Bell 202)

# PLL constants (matching C implementation)
TICKS_PER_PLL_CYCLE = 4294967296  # 2^32
DCD_THRESH_ON = 30
DCD_THRESH_OFF = 6
DCD_GOOD_WIDTH = 524288

MASK32 = np.uint32(0xFFFFFFFF)


class BitClk:
    """Python one-to-one replication of bitclk_t structure and algorithm."""

    def __init__(self, sample_rate: float, bit_rate: float):
        self.sample_rate = sample_rate
        self.bit_rate = bit_rate

        step = int((TICKS_PER_PLL_CYCLE * bit_rate) / sample_rate)
        self.pll_step_per_sample = np.int32(step)

        self._data_clock_pll = np.uint32(0)
        self.prev_demod_output = 0.0

        self.good_hist = np.uint64(0)
        self.bad_hist = np.uint64(0)
        self.score = np.uint64(0)
        self.data_detect = 0

        self.pll_locked_inertia = 0.74
        self.pll_searching_inertia = 0.50

        self.sampling_instants = []
        self.sampled_bits = []
        self.lock_states = []  # Track lock state at each sample

    @property
    def data_clock_pll(self) -> np.int32:
        return np.int32(self._data_clock_pll)

    @data_clock_pll.setter
    def data_clock_pll(self, value: np.int32):
        self._data_clock_pll = np.uint32(value)

    def _update_pll_lock_detection(self):
        pll_signed = self.data_clock_pll
        transition_near_zero = abs(pll_signed) < DCD_GOOD_WIDTH

        self.good_hist = (self.good_hist << 1) | (1 if transition_near_zero else 0)
        self.bad_hist = (self.bad_hist << 1) | (1 if not transition_near_zero else 0)

        good_count = bin(self.good_hist).count("1")
        bad_count = bin(self.bad_hist).count("1")
        self.score = (self.score << 1) | (1 if (good_count - bad_count) >= 2 else 0)

        score_count = bin(self.score).count("1")

        if score_count >= DCD_THRESH_ON and not self.data_detect:
            self.data_detect = 1
        elif score_count <= DCD_THRESH_OFF and self.data_detect:
            self.data_detect = 0

    def detect(self, soft_bit: float, sample_idx: int) -> int:
        sampled_bit = None
        prev_pll_value = self.data_clock_pll

        self._data_clock_pll = (self._data_clock_pll + np.uint32(self.pll_step_per_sample)) & MASK32

        current_pll = self.data_clock_pll
        if prev_pll_value > 0 and current_pll < 0:
            sampled_bit = 1 if soft_bit > 0.0 else 0
            self.sampling_instants.append(sample_idx)
            self.sampled_bits.append(sampled_bit)
            self.lock_states.append(self.data_detect)  # Store lock state at sampling
            self._update_pll_lock_detection()

        if (self.prev_demod_output < 0 and soft_bit > 0) or (
            self.prev_demod_output > 0 and soft_bit < 0
        ):
            denominator = soft_bit - self.prev_demod_output
            if abs(denominator) > 1e-6:
                fraction = -self.prev_demod_output / denominator
                target_phase = self.pll_step_per_sample * fraction
                inertia = self.pll_locked_inertia if self.data_detect else self.pll_searching_inertia
                new_pll_float = current_pll * inertia + target_phase * (1.0 - inertia)
                self.data_clock_pll = np.int32(round(new_pll_float))

        self.prev_demod_output = soft_bit
        return sampled_bit


def main():
    sr, data = wavfile.read("soft.wav")
    print(f"Loaded soft.wav: {len(data)} samples at {sr} Hz")

    if data.dtype != np.float32 and data.dtype != np.float64:
        data = data.astype(np.float32) / np.max(np.abs(data))

    sample_rate = sr if sr > 0 else SAMPLE_RATE
    samples = data[:NUM_SAMPLES]
    print(f"Processing {len(samples)} samples")

    bitclk = BitClk(sample_rate, BIT_RATE)

    bits = []
    for i, sample in enumerate(samples):
        bit = bitclk.detect(float(sample), i)
        if bit is not None:
            bits.append((i, bit))

    print(f"Detected {len(bits)} bits")

    # Single plot: soft bits with PLL sampling instants and lock state
    fig, ax = plt.subplots(figsize=(14, 5))
    ax.plot(samples, "b-", linewidth=0.5, label="Soft bits")
    ax.axhline(y=0, color="gray", linestyle="--", alpha=0.5)

    # Scatter plot: PLL sampling points (circle=locked, x=searching)
    samples_array = np.array(bitclk.sampling_instants)
    bits_array = np.array(bitclk.sampled_bits)
    lock_array = np.array(bitclk.lock_states)

    samples_np = np.array(samples)

    # Separate by bit value and lock state
    for bit_val in [0, 1]:
        for locked in [True, False]:
            mask = (bits_array == bit_val) & (lock_array == locked)
            if np.any(mask):
                x = samples_array[mask]
                y = samples_np[x]
                marker = "o" if locked else "x"
                color = "green" if bit_val == 1 else "red"
                label = f"bit={bit_val}, {'locked' if locked else 'searching'}"
                ax.scatter(x, y, color=color, s=40, marker=marker, label=label, zorder=5)

    ax.set_ylabel("Amplitude")
    ax.set_xlabel("Sample Index")
    ax.set_title(f"Soft Bits with PLL Sampling Instants (o=locked, x=searching; green=1, red=0) - {len(bits)} bits")
    ax.legend(loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
