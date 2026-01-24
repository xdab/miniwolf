#!/usr/bin/env python3
"""
BitClk - Python replication of C bit clock recovery algorithm.
Visualizes PLL sampling instants and lock state on soft bit waveform.
Uses floats in normalized range [-1.0, 1.0) for intuitive phase accumulation.
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile

# Configuration
NUM_SAMPLES = 22050  # Number of samples to process
SAMPLE_RATE = 22050  # Sample rate (fallback if not in WAV)
BIT_RATE = 1200  # Bit rate (Bell 202)

# PLL constants
DCD_THRESH_ON = 30
DCD_THRESH_OFF = 6
DCD_GOOD_THRESHOLD = 1.0 / 4096.0

# Float phase accumulator range
PHASE_MAX = 1.0
PHASE_MIN = -1.0
PHASE_WRAP = 2.0  # Distance to wrap


def wrap_phase(value: float) -> float:
    """Wrap phase to [-1.0, 1.0) range."""
    while value >= PHASE_MAX:
        value -= PHASE_WRAP
    while value < PHASE_MIN:
        value += PHASE_WRAP
    return value


class BitClk:
    """Python one-to-one replication of bitclk_t structure and algorithm."""

    def __init__(self, sample_rate: float, bit_rate: float):
        self.sample_rate = sample_rate
        self.bit_rate = bit_rate

        self.pll_step_per_sample = 2.0 * bit_rate / sample_rate
        self.data_clock_pll = 0.0
        self.prev_demod_output = 0.0

        self.good_hist = 0
        self.bad_hist = 0
        self.score = 0
        self.data_detect = 0

        self.pll_locked_inertia = 0.74
        self.pll_searching_inertia = 0.50

        self.sampling_instants = []
        self.sampled_bits = []
        self.lock_states = []  # Track lock state at each sample

        # Track DCD state over time
        self.dcd_scores = []  # Score at each sample
        self.dcd_states = []  # Data detect state at each sample
        self.phase_values = []  # PLL phase at each sample

    def _update_pll_lock_detection(self):
        phase_abs = abs(self.data_clock_pll)
        transition_near_zero = phase_abs < DCD_GOOD_THRESHOLD

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

        # Record DCD state and phase before advancing
        self.dcd_scores.append(bin(self.score).count("1"))
        self.dcd_states.append(self.data_detect)
        self.phase_values.append(self.data_clock_pll)

        # Advance PLL phase accumulator with wrapping
        self.data_clock_pll = wrap_phase(self.data_clock_pll + self.pll_step_per_sample)

        current_pll = self.data_clock_pll

        if prev_pll_value > 0.0 and current_pll < 0.0:
            sampled_bit = 1 if soft_bit > 0.0 else 0
            self.sampling_instants.append(sample_idx)
            self.sampled_bits.append(sampled_bit)
            self.lock_states.append(self.data_detect)
            self._update_pll_lock_detection()

        # Detect zero crossings for phase correction
        if (self.prev_demod_output < 0.0 and soft_bit > 0.0) or (
            self.prev_demod_output > 0.0 and soft_bit < 0.0
        ):
            denominator = soft_bit - self.prev_demod_output
            if abs(denominator) > 1e-6:
                fraction = -self.prev_demod_output / denominator
                target_phase = self.pll_step_per_sample * fraction
                inertia = (
                    self.pll_locked_inertia
                    if self.data_detect
                    else self.pll_searching_inertia
                )

                new_pll = current_pll * inertia + target_phase * (1.0 - inertia)
                self.data_clock_pll = wrap_phase(new_pll)

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

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True)

    # Top plot: soft bits with sampling instants
    ax1.plot(samples, "b-", linewidth=0.5, label="Soft bits")
    ax1.axhline(y=0, color="gray", linestyle="--", alpha=0.5)

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
                ax1.scatter(
                    x, y, color=color, s=40, marker=marker, label=label, zorder=5
                )

    ax1.set_ylabel("Amplitude")
    ax1.set_title(
        f"Soft Bits with PLL Sampling Instants (o=locked, x=searching; green=1, red=0) - {len(bits)} bits"
    )
    ax1.legend(loc="upper right", fontsize=8)
    ax1.grid(True, alpha=0.3)

    # Bottom plot: DCD score and state over time
    dcd_scores = np.array(bitclk.dcd_scores)
    dcd_states = np.array(bitclk.dcd_states)
    x_axis = np.arange(len(dcd_scores))

    # Plot DCD score as a filled area
    ax2.fill_between(x_axis, 0, dcd_scores, alpha=0.3, color="blue", label="DCD score")
    ax2.plot(x_axis, dcd_scores, "b-", linewidth=0.8)

    # Plot DCD threshold lines
    ax2.axhline(y=DCD_THRESH_ON, color="green", linestyle="--", alpha=0.7, label=f"ON threshold ({DCD_THRESH_ON})")
    ax2.axhline(y=DCD_THRESH_OFF, color="red", linestyle="--", alpha=0.7, label=f"OFF threshold ({DCD_THRESH_OFF})")

    # Plot DCD state (1=locked, 0=searching) as step function
    ax2.step(x_axis, dcd_states, where="post", color="black", linewidth=1.5, label="DCD state")
    ax2.fill_between(x_axis, 0, dcd_states, step="post", alpha=0.2, color="green")

    ax2.set_ylabel("DCD Score / State")
    ax2.set_xlabel("Sample Index")
    ax2.set_title("DCD (Data Carrier Detect) State Over Time")
    ax2.legend(loc="upper right", fontsize=8)
    ax2.grid(True, alpha=0.3)
    ax2.set_ylim(-1, max(DCD_THRESH_ON + 5, dcd_scores.max() + 5))

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
