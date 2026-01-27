#!/usr/bin/env python3
"""
BitClock detector replica in Python for debugging low sample rate issues.
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy import signal
import sys

PHASE_MAX = 1.0
PHASE_MIN = -1.0
PHASE_WRAP = 2.0

# Threshold for 'good' zero crossing in units of bit periods
DCD_GOOD_TIMING_THRESHOLD = 0.05
DCD_THRESH_ON = 26
DCD_THRESH_OFF = 12

BITCLK_NONE = -1


def wrap_phase(value):
    """Wrap phase to [-1.0, 1.0) range."""
    while value >= PHASE_MAX:
        value -= PHASE_WRAP
    while value < PHASE_MIN:
        value += PHASE_WRAP
    return value


class BitClk:
    """Python replica of bitclk_t and bitclk_detect logic."""

    def __init__(self, sample_rate, bit_rate):
        """Initialize BitClock detector."""
        self.pll_clock_tick = 2.0 * bit_rate / sample_rate
        self.pll_clock = 0.0
        self.last_soft_bit = 0.0

        self.good_hist = 0
        self.score = 0
        self.data_detect = 0

        self.pll_max_inertia = 0.82
        self.pll_min_interia = 0.28

        # Visualization tracking
        self.pll_history = []
        self.sampled_bits = []
        self.is_locked_history = []
        self.zero_crossing_sample_indices = []
        self.zero_crossing_sub_sample_fractions = []
        self.zero_crossing_pll_values = []
        self.zero_crossing_is_good = []  # Track good vs bad crossings
        self.inertia_history = []  # Track inertia values over time
        self.interpolated_symbol_times = []  # Times where symbols are sampled
        self.interpolated_symbol_values = []  # Interpolated symbol values at sampling times
        self.symbol_sample_indices = []  # Sample indices where symbols are recovered

    def update_lock_detection(self, timing_error_in_bit_periods):
        """Update PLL lock state based on zero crossing timing accuracy.

        Args:
            timing_error_in_bit_periods: How far off the crossing was, in units of bit periods.
                Positive means PLL was ahead, negative means behind.
        """
        is_good_transition = (
            abs(timing_error_in_bit_periods) < DCD_GOOD_TIMING_THRESHOLD
        )

        # Record for visualization
        self.zero_crossing_is_good.append(is_good_transition)

        # Shift and update history registers
        self.good_hist = ((self.good_hist << 1) & 0xFFFFFFFF) | (
            1 if is_good_transition else 0
        )

        # Determine lock state from score
        self.score = bin(self.good_hist).count("1")
        if self.score >= DCD_THRESH_ON and not self.data_detect:
            self.data_detect = 1
            print(f"PLL locked (score bits: {self.score})")
        elif self.score <= DCD_THRESH_OFF and self.data_detect:
            self.data_detect = 0
            print(f"PLL unlocked (score bits: {self.score})")

    def detect(self, soft_bit, sample_idx=0):
        """Process one soft bit and update PLL state."""
        # Advance PLL by one tick
        prev_pll = self.pll_clock
        curr_pll = prev_pll + self.pll_clock_tick
        self.pll_clock = wrap_phase(curr_pll)

        # Sample bit when PLL wraps through -1.0 (falling edge)
        sampled_bit = BITCLK_NONE
        if prev_pll > 0.0 and self.pll_clock < 0.0:
            # Find where PLL would be exactly +1.0 (wrapping point)
            # Using curr_pll which is > 1.0 when wrapping occurs
            # fraction: 0 = at last sample, 1 = at current sample
            if abs(self.pll_clock_tick) > 1e-6:
                fraction_to_max = (PHASE_MAX - prev_pll) / self.pll_clock_tick
                if 0.0 <= fraction_to_max <= 1.0:
                    # Interpolate soft_bit value at this point
                    soft_bit_at_sample = self.last_soft_bit + fraction_to_max * (soft_bit - self.last_soft_bit)
                    sampled_bit = 1 if soft_bit_at_sample > 0.0 else 0
                    self.symbol_sample_indices.append(sample_idx)
                    self.interpolated_symbol_times.append(fraction_to_max)
                    self.interpolated_symbol_values.append(soft_bit_at_sample)

        # Check for signal zero crossing
        if self.last_soft_bit * soft_bit < 0.0:
            self.zero_crossing_sample_indices.append(sample_idx)

            # Find precise sub-sample location of zero crossing
            signal_delta = soft_bit - self.last_soft_bit
            if abs(signal_delta) > 1e-6:
                # Fraction: 0 = at last sample, 1 = at current sample
                fraction = -self.last_soft_bit / signal_delta
                self.zero_crossing_sub_sample_fractions.append(fraction)

                # Evaluate PLL value at exact crossing point
                pll_at_crossing = prev_pll + self.pll_clock_tick * fraction
                self.zero_crossing_pll_values.append(pll_at_crossing)

                # Calculate when PLL will naturally cross zero (in samples from exact crossing time)
                samples_to_pll_zero = -pll_at_crossing / self.pll_clock_tick

                # Timing error: how many samples away is the PLL zero crossing from signal zero crossing
                timing_error_in_samples = abs(samples_to_pll_zero)

                # Convert to bit periods (one bit period = 2.0 / pll_clock_tick samples)
                timing_error_in_bit_periods = (
                    timing_error_in_samples * self.pll_clock_tick / 2.0
                )

                self.update_lock_detection(timing_error_in_bit_periods)

                inertia = self.pll_min_interia + (self.pll_max_inertia - self.pll_min_interia) * (self.score / 32.0)
                self.inertia_history.append(inertia)
                ideal_pll = (1 - fraction) * self.pll_clock_tick
                self.pll_clock = self.pll_clock * inertia + ideal_pll * (1.0 - inertia)

        self.last_soft_bit = soft_bit

        # Record history for visualization
        self.pll_history.append(self.pll_clock)
        self.sampled_bits.append(sampled_bit)
        self.is_locked_history.append(self.data_detect)

        return sampled_bit

    def process_buffer(self, soft_bits):
        """Process a buffer of soft bits."""
        for idx, soft_bit in enumerate(soft_bits):
            self.detect(soft_bit, sample_idx=idx)


def load_wav(filename):
    """Load WAV file and return sample rate and samples."""
    sample_rate, data = wavfile.read(filename)

    # Convert to float and normalize if needed
    if data.dtype == np.int16:
        data = data.astype(np.float32) / 32768.0
    elif data.dtype == np.uint8:
        data = (data.astype(np.float32) - 128) / 128.0
    elif data.dtype != np.float32 and data.dtype != np.float64:
        data = data.astype(np.float32)

    # Handle stereo -> mono
    if len(data.shape) > 1:
        data = data[:, 0]

    return sample_rate, data


def resample_audio(samples, orig_rate, target_rate):
    """Resample audio to target rate."""
    if orig_rate == target_rate:
        return samples

    ratio = target_rate / orig_rate
    num_samples = int(len(samples) * ratio)
    return signal.resample(samples, num_samples)


def main():
    """Main debugging script."""
    wav_file = "/home/pw/Desktop/miniwolf/soft.wav"

    # Load original WAV
    orig_rate, orig_samples = load_wav(wav_file)
    print(f"Loaded {wav_file}")
    print(f"Original sample rate: {orig_rate} Hz")
    print(f"Duration: {len(orig_samples) / orig_rate:.3f} seconds")
    print(f"Samples: {len(orig_samples)}")

    # Extract portion
    start_time = 1.2
    end_time = 1.22
    start_idx = int(start_time * orig_rate)
    end_idx = int(end_time * orig_rate)
    orig_samples = orig_samples[start_idx:end_idx]
    print(f"\nExtracted portion from {start_time}s to {end_time}s")
    print(f"Extracted samples: {len(orig_samples)}")

    # Test parameters (typical for modem)
    bit_rate = 1200.0  # Common modem bit rate - adjust if needed

    # Test at different sample rates
    test_rates = [8000]
    results = {}

    fig, axes = plt.subplots(len(test_rates), 1, figsize=(14, 7))
    if len(test_rates) == 1:
        axes = [axes]

    for idx, target_rate in enumerate(test_rates):
        print(f"\n--- Testing at {target_rate} Hz ---")

        # Resample if needed
        if target_rate == orig_rate:
            samples = orig_samples
        else:
            samples = resample_audio(orig_samples, orig_rate, target_rate)
            print(f"Resampled to {target_rate} Hz ({len(samples)} samples)")

        # Initialize detector
        detector = BitClk(target_rate, bit_rate)
        detector.process_buffer(samples)

        results[target_rate] = detector

        # Plot
        ax = axes[idx]
        time_axis = np.arange(len(samples)) / target_rate * 1000  # milliseconds

        # Plot soft bits with sample point markers
        ax.plot(
            time_axis,
            samples,
            "b-",
            alpha=0.7,
            # linewidth=1.5,
            # marker="o",
            # markersize=2,
            label="Soft bits",
        )

        # Plot PLL clock with sample point markers
        pll_values = np.array(detector.pll_history)
        ax.plot(
            time_axis,
            pll_values,
            "r-",
            alpha=0.5,
            linewidth=1,
            marker="o",
            markersize=1,
            label="PLL clock",
        )

        # Plot inertia as a separate line
        if detector.inertia_history:
            # Align inertia history with time axis (inertia is recorded at zero crossings)
            inertia_values = np.array(detector.inertia_history)
            inertia_times = []
            for idx in detector.zero_crossing_sample_indices[:len(inertia_values)]:
                inertia_times.append(time_axis[idx] if idx < len(time_axis) else time_axis[-1])
            
            if inertia_times:
                ax.plot(
                    inertia_times,
                    inertia_values,
                    "o-",
                    color="purple",
                    alpha=0.7,
                    linewidth=2,
                    markersize=4,
                    label="Inertia",
                )

        # Mark sampled bits
        sampled_times = []
        sampled_values = []
        for i, bit in enumerate(detector.sampled_bits):
            if bit != BITCLK_NONE:
                sampled_times.append(time_axis[i])
                sampled_values.append(samples[i])
        if sampled_times:
            ax.scatter(
                sampled_times,
                sampled_values,
                c="green",
                s=30,
                marker="x",
                linewidths=2,
                label="Sampled bits",
                zorder=5,
            )

        # Plot interpolated symbol values at their exact sampling points
        if detector.interpolated_symbol_values:
            symbol_times = []
            for i, sample_idx in enumerate(detector.symbol_sample_indices):
                if i < len(detector.interpolated_symbol_times):
                    fraction = detector.interpolated_symbol_times[i]
                    time_step = time_axis[sample_idx] - time_axis[sample_idx - 1] if sample_idx > 0 else time_axis[0]
                    exact_time = time_axis[sample_idx - 1] + time_step * fraction if sample_idx > 0 else time_axis[0]
                    symbol_times.append(exact_time)
            
            if symbol_times:
                ax.scatter(
                    symbol_times,
                    detector.interpolated_symbol_values,
                    c="cyan",
                    s=40,
                    marker="o",
                    alpha=0.8,
                    label="Interpolated symbols",
                    zorder=6,
                )
        #     )

        # Visualize zero crossings: bold line, signal crossing point, and PLL value
        for i, zc_idx in enumerate(detector.zero_crossing_sample_indices):
            if not (0 < zc_idx < len(samples)):
                continue

            # Calculate exact crossing time
            if i >= len(detector.zero_crossing_sub_sample_fractions):
                continue

            fraction = detector.zero_crossing_sub_sample_fractions[i]
            time_step = time_axis[zc_idx] - time_axis[zc_idx - 1]
            exact_time = time_axis[zc_idx - 1] + time_step * fraction

            # Add vertical stripe: green for good, red for bad
            if i < len(detector.zero_crossing_is_good):
                stripe_color = "green" if detector.zero_crossing_is_good[i] else "red"
                ax.axvline(exact_time, color=stripe_color, alpha=0.1, linewidth=1)

            # Mark PLL value at exact crossing time with orange circle
            if i < len(detector.zero_crossing_pll_values):
                pll_at_crossing = detector.zero_crossing_pll_values[i]
                ax.plot(
                    exact_time,
                    pll_at_crossing,
                    "o",
                    color="orange",
                    markersize=8,
                    zorder=6,
                )

        # Mark lock status transitions with vertical lines
        for i in range(1, len(detector.is_locked_history)):
            if detector.is_locked_history[i] != detector.is_locked_history[i - 1]:
                ax.axvline(
                    time_axis[i],
                    color="green" if detector.is_locked_history[i] else "red",
                    linestyle="--",
                    alpha=0.6,
                    linewidth=4,
                )

        ax.set_ylabel("Amplitude")
        ax.set_title(
            f"Sample Rate: {target_rate} Hz (Bit Rate: {bit_rate} Hz, pll_tick: {detector.pll_clock_tick:.4f})"
        )
        ax.legend(loc="upper right", fontsize=8)
        ax.grid(True, alpha=0.3)

        # Print statistics
        num_sampled_bits = sum(1 for b in detector.sampled_bits if b != BITCLK_NONE)
        lock_time_ms = sum(detector.is_locked_history) / target_rate * 1000
        print(f"  Bits sampled: {num_sampled_bits}")
        print(f"  Lock duration: {lock_time_ms:.1f} ms")
        print(f"  Final lock status: {detector.data_detect}")

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
