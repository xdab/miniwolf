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


def read_f32(path):
    x = np.fromfile(path, dtype=np.float32)
    if x.size == 0:
        sys.exit(f"{path}: empty file")
    return x


def bandpass_real(x, rate, fc=1700.0, sigma=1000.0):
    """Two-sided gaussian bandpass around the AFSK tone pair, real domain.
    The discriminator hiss outside the tone band otherwise corrupts the
    analytic phase: the instantaneous-frequency readout of raw audio fails
    below C/N ~20 (unwrap breaks, biases of hundreds of Hz), while the
    band-limited signal measures cleanly down to C/N 10. Signal-neutral:
    the composition mean shifts < 0.1 Hz on noise-free input."""
    X = np.fft.fft(x.astype(np.float64))
    f = np.fft.fftfreq(len(X), 1.0 / rate)
    X *= np.exp(-0.5 * ((f - fc) / sigma) ** 2) + np.exp(-0.5 * ((f + fc) / sigma) ** 2)
    return np.fft.ifft(X).real


def inst_freq(x, rate):
    """Instantaneous frequency (Hz, per-sample) via the analytic signal."""
    X = np.fft.fft(x.astype(np.float64))
    H = np.zeros(len(X))
    H[0] = 1
    H[1:len(X) // 2] = 2
    ph = np.unwrap(np.angle(np.fft.ifft(X * H)))
    return np.diff(ph) * rate / (2.0 * np.pi)


def flag_span(f, start, spb):
    """Search one stable-tone region for the flag pattern: correlate the
    region's per-bit tone means against the M*7 S*1 pattern (0x7E NRZI,
    period 8 bits) over every whole-period window length >= HEAD_FLAGS_MIN
    and every lag, via FFT. Data is not periodic, so the correlation
    collapses once the window overruns into it; the tone-pair shift moves
    both tones equally, so the pattern matches at any shift, and polarity
    is folded into |corr|. Returns (lag_cells, periods, corr, mean_hz) or
    None. The mean tone over the matched span needs no bit alignment: any
    8p-bit span of the periodic region averages exactly 1325 Hz."""
    max_bits = HEAD_FLAGS_MAX * 8
    if start + (max_bits + 8) * spb > len(f):
        end = len(f)
    else:
        end = start + (max_bits + 8) * spb
    g = f[start:end]
    nbits = len(g) // spb
    if nbits < HEAD_FLAGS_MIN * 8:
        return None
    cells = g[:nbits * spb].reshape(nbits, spb)[:, spb // 4:3 * spb // 4].mean(axis=1)
    period = np.array([1.0] * 7 + [-1.0])
    period -= period.mean()
    F = np.fft.rfft(cells)
    # the preamble IS the longest run of consecutive flag periods, so for
    # each lag keep the largest window length whose correlation holds above
    # MIN_CORR; data-section flag delimiters match too but only for 1-2
    # periods, so the preamble wins on length, not on correlation
    best_p = np.zeros(nbits + 1, dtype=int)
    best_r = np.zeros(nbits + 1)
    # 0.8: pure-periodic windows measure 0.95+ in the bandpassed domain at
    # every C/N; windows overrunning into data decay to ~0.5, so the bar
    # marks where periodicity actually ends (noise windows never reach it)
    RUN_CORR = 0.8
    for p in range(HEAD_FLAGS_MIN, HEAD_FLAGS_MAX + 1):
        m = p * 8
        if m > nbits:
            break
        ref = np.tile(period, p)
        q = ref - ref.mean()
        num = np.fft.irfft(F * np.conj(np.fft.rfft(q, len(cells))), len(cells))[:nbits - m + 1]
        w1 = np.convolve(cells, np.ones(m), 'valid')
        w2 = np.convolve(cells * cells, np.ones(m), 'valid')
        var = w2 / m - (w1 / m) ** 2
        r = np.abs(num) / (np.sqrt(np.maximum(var, 1e-9) * m) * np.sqrt((q * q).sum()))
        ok = r >= 0.6                       # candidate bar; sub-bit refined
                                            # correlation decides acceptance
        upd = ok & (p > best_p[:len(r)])
        best_p[:len(r)][upd] = p
        best_r[:len(r)][upd] = r[upd]
    if best_p.max() < HEAD_FLAGS_MIN:
        return None
    # among the longest periodic windows, take the best-correlating lag
    # (ties are common: any whole-period window inside the preamble matches)
    maxp = best_p.max()
    cands = np.where(best_p == maxp)[0]
    lag = int(cands[np.argmax(best_r[cands])])
    p = int(maxp)
    # sub-bit alignment: the correlation grid is 1-cell, but the length
    # decision needs cells aligned to the bit boundaries -- rescan the grid
    # offset for this window only
    trim = spb // 4
    q = np.tile(period, p)
    q -= q.mean()
    best = None
    for off in range(spb):
        cg = f[start + off:start + off + nbits * spb].reshape(nbits, spb)
        cc = cg[:, trim:spb - trim].mean(axis=1)
        w = cc[lag:lag + p * 8] - cc[lag:lag + p * 8].mean()
        d = np.sqrt((w * w).sum() * (q * q).sum())
        r = abs(float((w * q).sum() / d)) if d > 0 else 0.0
        if best is None or r > best[0]:
            best = (r, off, cc)
    r, off, cc = best
    # shrink overrun: the longest candidate window may include data bits;
    # walk down to the largest length measuring pure periodicity (>= 0.95
    # refined; a window shorter than the preamble is unbiased, one longer
    # is not). Noise windows never reach the acceptance bar at any length.
    p = maxp
    while p > HEAD_FLAGS_MIN:
        q = np.tile(period, p)
        q -= q.mean()
        w = cc[lag:lag + p * 8] - cc[lag:lag + p * 8].mean()
        d = np.sqrt((w * w).sum() * (q * q).sum())
        r = abs(float((w * q).sum() / d)) if d > 0 else 0.0
        if r >= 0.95:
            break
        p -= 1
    q = np.tile(period, p)
    q -= q.mean()
    w = cc[lag:lag + p * 8] - cc[lag:lag + p * 8].mean()
    d = np.sqrt((w * w).sum() * (q * q).sum())
    r = abs(float((w * q).sum() / d)) if d > 0 else 0.0
    if r < 0.8:
        return None                         # refined corr: noise never gets here

    # mean tone over the whole matched span with a small edge guard: any
    # 8p-bit span of the periodic region averages exactly 1325 Hz, and the
    # guard drops run-start / overrun junk concentrated at the window edges
    s = start + lag * spb + off
    n = p * 8 * spb
    guard = spb // 4
    m = float(np.mean(f[s + guard:s + n - guard]))
    return lag, p, r, m


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-f", "--file", required=True, help="input raw f32le audio file")
    p.add_argument("-r", "--rate", type=float, default=48000.0, help="sample rate Hz")
    args = p.parse_args()

    x = bandpass_real(read_f32(args.file), args.rate)
    f = inst_freq(x, args.rate)
    # rolling scatter of the instantaneous frequency over 10 ms windows:
    # the noise floor between frames is wide-band hiss, the preamble is tones
    win = int(0.01 * args.rate)
    std = np.sqrt(np.convolve(f * f, np.ones(win), mode="valid") / win
                  - np.convolve(f, np.ones(win), mode="valid") ** 2 / win ** 2)
    # relative stability gate: in the bandpassed domain the preamble's
    # scatter is ~0.25x the noise-floor scatter at every C/N (limiter-clipped
    # hiss is saturated in-band); p90 lands in the noise floor for typical
    # captures, and half of it separates tones from hiss with margin
    stable = np.where(std < 0.5 * np.percentile(std, 90))[0]
    if len(stable) == 0:
        print("no stable-tone region found")
        return 1
    spb = int(args.rate / BAUD)
    for run in np.split(stable, np.where(np.diff(stable) > 1)[0] + 1):
        hit = flag_span(f, int(run[0]), spb)
        if hit is None:
            continue
        lag, periods, r, m = hit
        print(f"preamble at sample {int(run[0]) + lag * spb} "
              f"(t={(int(run[0]) + lag * spb) / args.rate * 1000:.1f} ms), "
              f"{periods} flags (corr {r:.2f}): mean inst-freq {m:8.2f} Hz, "
              f"expected {EXPECTED_MEAN:.1f} "
              f"-> tone-pair shift {m - EXPECTED_MEAN:+8.2f} Hz")
        return 0
    print("no flag-pattern preamble found")
    return 1


if __name__ == "__main__":
    sys.exit(main())
