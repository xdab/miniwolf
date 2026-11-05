#!/usr/bin/env python3
"""Channel impairments shared by tools/frame.py and tools/ber.py.

One definition each of the degradation arms, so the generator CLI and the
sweep tool cannot drift apart. Every function takes the already-modulated
audio and returns degraded audio; nothing here touches the modulator, and
everything stochastic is driven by a caller-owned np.random.Generator so a
--seed draws the same channel in every tool (paired A/B).
"""

import numpy as np


def add_awgn(x: np.ndarray, snr_db: float, rng: np.random.Generator) -> np.ndarray:
    """Real AWGN at the audio SNR: 10*log10(mean(x^2) / E[n^2]) over the file.

    Noise spans the whole file, silence padding included: the file-average
    signal power is diluted by the leading/trailing zeros, biasing the
    applied SNR low by a fraction of a dB (the same accepted bias as
    rxx_lora's ber.py). A pure function of (seed, size): paired A/B sweeps
    see identical noise.
    """
    sigma = np.sqrt(np.mean(x.astype(np.float64) ** 2) * 10.0 ** (-snr_db / 10.0))
    return np.asarray(x + sigma * rng.standard_normal(x.size), dtype=np.float32)


def cfo_offset(x: np.ndarray, rate: float, hz: float) -> np.ndarray:
    """Frequency offset: real carrier multiply by cos(2*pi*hz*t).

    A real translation — the tone pair moves together (1200->1200+hz,
    2200->2200+hz) and an image term appears at the mirror frequencies,
    which is what an off-tune receiver plus its audio band actually sees.
    """
    t = np.arange(x.size, dtype=np.float64) / rate
    return np.asarray(x * np.cos(2.0 * np.pi * hz * t), dtype=np.float32)


def ppm_drift(x: np.ndarray, ppm: float) -> np.ndarray:
    """Sample-rate error: resample by (1 + ppm*1e-6), i.e. the transmitter's
    clock runs away from the receiver's. Linear interpolation — at the
    tens-of-ppm this arm targets the interpolation error sits far below the
    noise the demod already survives; the point is the accumulating bit-clock
    drift into src/bitclk.c's PLL, not resampling fidelity. Duration changes
    by ppm*duration (nanoseconds per sample); the tail gains/loses less than
    one sample at sweep-relevant rates, which the caller ignores."""
    ratio = 1.0 + ppm * 1e-6
    n_out = int(round(x.size / ratio))
    pos = np.arange(n_out, dtype=np.float64) * ratio
    return np.asarray(np.interp(pos, np.arange(x.size), x), dtype=np.float32)


def frac_delay(x: np.ndarray, samples: float) -> np.ndarray:
    """Fractional sample delay (band-limited approximation via linear
    interpolation): constant bit-clock phase error at the sampler boundary.
    """
    if samples <= 0:
        return x
    pos = np.arange(x.size, dtype=np.float64) - samples
    y = np.interp(pos, np.arange(x.size), x)
    y[pos < 0] = 0.0  # interp extrapolates flat before the first sample
    return np.asarray(y, dtype=np.float32)


def squelch_burst(x: np.ndarray, rate: float, rng: np.random.Generator,
                  duration_s: float = 0.03) -> np.ndarray:
    """One squelch burst: `duration_s` of hard-zeroed audio at a position
    drawn uniformly from the frame interior. The packet-eater of real FM:
    tens of ms of nothing mid-frame, exercising DCD hysteresis unlock and
    the bitclk PLL's re-acquisition. Draw is a pure function of (seed, size)."""
    dur = max(1, int(duration_s * rate))
    start = int(rng.integers(1, max(2, x.size - dur - 1)))
    y = x.copy()
    y[start:start + dur] = 0.0
    return y
