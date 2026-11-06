#!/usr/bin/env python3
"""Measure the AFSK tone-pair shift (data carriers off nominal 1200/2200 Hz).

Read the file, take the instantaneous frequency, locate stable-tone
regions (noise floor is wide-band hiss, tones are not), and within each
region correlate the per-bit tone means against the flag-symbol pattern
(0x7E NRZI = M*7 S*1 repeating, period 8 bits) over every sub-bit offset,
pattern phase, polarity and whole-period length. The best-matching window
is the preamble: data is not periodic, so the correlation collapses once
the window overruns into it. The mean instantaneous frequency over the
matched cells' interiors minus (7*1200 + 2200)/8 = 1325 Hz is the
tone-pair shift.

    python3 tools/cfo.py -f rx.f32 -r 48000
"""

import argparse
import sys

import numpy as np

EXPECTED_MEAN = (7 * 1200.0 + 2200.0) / 8.0    # 1325 Hz, M*7 S*1
BAUD = 1200.0
HEAD_FLAGS_MIN = 4                             # whole flag periods to require
HEAD_FLAGS_MAX = 32
MIN_CORR = 0.5                                 # flag-pattern match quality
STABLE_STD = 2500.0                            # Hz; tones vs hiss scatter


def read_f32(path):
    x = np.fromfile(path, dtype=np.float32)
    if x.size == 0:
        sys.exit(f"{path}: empty file")
    return x


def inst_freq(x, rate):
    """Instantaneous frequency (Hz, per-sample) via the analytic signal."""
    X = np.fft.fft(x.astype(np.float64))
    H = np.zeros(len(X))
    H[0] = 1
    H[1:len(X) // 2] = 2
    ph = np.unwrap(np.angle(np.fft.ifft(X * H)))
    return np.diff(ph) * rate / (2.0 * np.pi)


def flag_span(f, start, spb):
    """Search one stable-tone region for the flag pattern: per-bit tone
    means at every sub-bit offset, correlated against the M*7 S*1 pattern
    over every phase, polarity and whole-period length (>= HEAD_FLAGS_MIN).
    Returns (off, b, periods, corr, mean_hz) of the best window, or None.
    Data is not periodic, so the correlation collapses once the window
    overruns into it; the tone-pair shift moves both tones equally, so the
    pattern matches at any shift, and polarity covers NRZI conventions."""
    nb = HEAD_FLAGS_MAX * 8 + 8
    if start + (nb + 1) * spb > len(f):
        return None
    g = f[start:start + (nb + 1) * spb]
    trim = spb // 4
    ref1 = np.tile(np.array([1.0] * 7 + [-1.0]), HEAD_FLAGS_MAX + 1)
    ref1 -= ref1.mean()
    best = None
    for off in range(spb):                       # sub-bit alignment, step 1:
        cells = g[off:off + nb * spb].reshape(nb, spb)[:, trim:spb - trim].mean(axis=1)
        for b in range(8):                       # flag-pattern phase
            for sign in (1.0, -1.0):             # NRZI polarity
                ref = sign * np.roll(ref1, -b)
                for p in range(HEAD_FLAGS_MIN, HEAD_FLAGS_MAX + 1):
                    w = cells[b:b + p * 8] - cells[b:b + p * 8].mean()
                    q = ref[:p * 8] - ref[:p * 8].mean()
                    d = np.sqrt((w * w).sum() * (q * q).sum())
                    r = float((w * q).sum() / d) if d > 0 else 0.0
                    if best is None or r > best[0]:
                        best = (r, off, b, sign, p)
    if best is None or best[0] < MIN_CORR:
        return None
    r, off, b, sign, p_best = best
    # prefer the longest window whose match is statistically as good: every
    # whole-period length inside the preamble correlates equally well, so
    # the argmax lands on small p by noise alone and throws away averaging
    cells = g[off:off + nb * spb].reshape(nb, spb)[:, trim:spb - trim].mean(axis=1)
    ref = sign * np.roll(ref1, -b)
    p = p_best
    for pp in range(HEAD_FLAGS_MAX, HEAD_FLAGS_MIN - 1, -1):
        w = cells[b:b + pp * 8] - cells[b:b + pp * 8].mean()
        q = ref[:pp * 8] - ref[:pp * 8].mean()
        d = np.sqrt((w * w).sum() * (q * q).sum())
        rp = float((w * q).sum() / d) if d > 0 else 0.0
        if rp >= r - 0.02:
            p = pp
            break
    # mean tone over the whole matched span with a small edge guard: any
    # 8p-bit span of the periodic region averages exactly 1325 Hz, and the
    # guard drops the run-start / overrun junk concentrated at the window
    # edges (per-bit interior trimming, by contrast, samples the hilbert
    # reconstruction ripple inside every bit and biases the mean several Hz)
    s = start + off + b * spb
    n = p * 8 * spb
    guard = spb // 4
    m = float(np.mean(f[s + guard:s + n - guard]))
    return off, b, p, r, m


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-f", "--file", required=True, help="input raw f32le audio file")
    p.add_argument("-r", "--rate", type=float, default=48000.0, help="sample rate Hz")
    args = p.parse_args()

    x = read_f32(args.file)
    f = inst_freq(x, args.rate)
    # rolling scatter of the instantaneous frequency over 10 ms windows:
    # the noise floor between frames is wide-band hiss, the preamble is tones
    win = int(0.01 * args.rate)
    std = np.sqrt(np.convolve(f * f, np.ones(win), mode="valid") / win
                  - np.convolve(f, np.ones(win), mode="valid") ** 2 / win ** 2)
    stable = np.where(std < STABLE_STD)[0]
    if len(stable) == 0:
        print("no stable-tone region found")
        return 1
    spb = int(args.rate / BAUD)
    for run in np.split(stable, np.where(np.diff(stable) > 1)[0] + 1):
        hit = flag_span(f, int(run[0]), spb)
        if hit is None:
            continue
        off, b, periods, r, m = hit
        print(f"preamble at sample {int(run[0]) + off} "
              f"(t={(int(run[0]) + off) / args.rate * 1000:.1f} ms), "
              f"{periods} flags (corr {r:.2f}): mean inst-freq {m:8.2f} Hz, "
              f"expected {EXPECTED_MEAN:.1f} "
              f"-> tone-pair shift {m - EXPECTED_MEAN:+8.2f} Hz")
        return 0
    print("no flag-pattern preamble found")
    return 1


if __name__ == "__main__":
    sys.exit(main())
