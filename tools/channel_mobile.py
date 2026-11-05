#!/usr/bin/env python3
"""VHF mobile/fading channel: Bell202 audio in, faded FM audio out.

The chain a real 2 m mobile link applies to AFSK audio: the Bell202 signal
frequency-modulates an RF carrier (NFM), the complex-baseband carrier passes
through a Clarke multipath channel at the vehicle speed (Rayleigh; Rician
with a K factor), receiver thermal noise is added at the chosen RF C/N, and
an ideal limiter + phase-difference FM demodulator recovers the audio —
including the click/pop static bursts real radios produce inside deep fades
(the FM threshold effect). The output is shaped exactly like what miniwolf's
soundcard input would see, so it feeds mw_bench or the live chain unchanged.

The noise reference for this arm is RF C/N (carrier-to-noise at the
demodulator input), NOT audio SNR — inside fades the discriminator is past
threshold and audio SNR collapses regardless of the C/N number. The dual
audio-SNR/RF-C/N accounting in TODOS.md is the bookkeeping layer for that.

Standalone use:
    python3 tools/frame.py --text hi -o clean.f32
    python3 tools/channel_mobile.py -i clean.f32 -o mobile.f32 \
        --speed 100 --cnr 25 --seed 1
    build/mw_bench -f mobile.f32 -r 48000
"""

import argparse
import os
import sys

import numpy as np


def fm_modulate(audio: np.ndarray, rate: float, deviation: float) -> np.ndarray:
    """Complex-baseband NFM: phase = 2*pi*k*integral(audio), unit amplitude."""
    phase = 2.0 * np.pi * deviation * np.cumsum(audio.astype(np.float64)) / rate
    return np.exp(1j * phase)


def clarke_channel(n: int, rate: float, fd: float, rng: np.random.Generator,
                   k: float = 0.0, n_paths: int = 50) -> np.ndarray:
    """Clarke/Rayleigh time-varying complex gain: n_paths scatterers, each
    from a uniform arrival angle (so its Doppler is fd*cos(angle)) with
    random phase; sum normalized to unit average power. k > 0 adds a
    constant LOS component at sqrt(k) relative power (Rician): the
    pure-Rayleigh k=0 case is mobiles in multipath, large k is a
    mostly-open road."""
    t = np.arange(n, dtype=np.float64) / rate
    channel = np.zeros(n, dtype=np.complex128)
    for _ in range(n_paths):
        angle = rng.uniform(0.0, 2.0 * np.pi)
        phi = rng.uniform(0.0, 2.0 * np.pi)
        channel += np.exp(1j * (2.0 * np.pi * fd * np.cos(angle) * t + phi))
    channel /= np.sqrt(n_paths)
    if k > 0:
        # LOS at Doppler 0 (driving straight at the repeater is the extreme;
        # the LOS Doppler is bounded by fd anyway, and 0 keeps the mean stable)
        los = np.sqrt(k) * np.ones(n, dtype=np.complex128)
        channel = (channel + los) / np.sqrt(1.0 + k)
    return channel


def fm_demodulate(iq: np.ndarray, rate: float, deviation: float,
                  limiter: float = 3.0) -> np.ndarray:
    """Phase-difference discriminator with a hard limiter: amplitude is
    discarded (the capture effect), and discriminator output is clipped to
    +-limiter — the stand-in for the audio clamping real radios do on
    threshold clicks."""
    phase = np.unwrap(np.angle(iq))
    audio = np.diff(phase) * rate / (2.0 * np.pi * deviation)
    audio = np.append(audio, 0.0)
    return np.asarray(np.clip(audio, -limiter, limiter), dtype=np.float32)


def mobile_channel(audio: np.ndarray, rate: float, rng: np.random.Generator,
                   fc: float = 145.0e6, speed_kph: float = 100.0, k: float = 0.0,
                   cnr_db: float = 30.0, deviation: float = 3000.0,
                   paths: int = 50, limiter: float = 3.0) -> np.ndarray:
    """The whole mobile link: FM modulate -> multipath fade -> thermal noise
    at the RF C/N -> limiter discriminator. fd from physical speed at the
    carrier (100 km/h at 145 MHz is ~13.4 Hz Doppler spread); noise power is
    set against the clean carrier power so C/N means what a radio spec means."""
    c = 3.0e8
    fd = (speed_kph / 3.6) / (c / fc)
    tx = fm_modulate(audio, rate, deviation)
    faded = tx * clarke_channel(tx.size, rate, fd, rng, k, paths)
    cnr = 10.0 ** (cnr_db / 10.0)
    sigma = np.sqrt(np.mean(np.abs(tx) ** 2) / cnr / 2.0)
    noise = sigma * (rng.standard_normal(tx.size)
                     + 1j * rng.standard_normal(tx.size))
    return fm_demodulate(faded + noise, rate, deviation, limiter)


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-i", "--in", dest="infile", required=True, help="input raw f32le audio file")
    p.add_argument("-o", "--out", default="mobile.f32", help="output raw f32le file (default: mobile.f32)")
    p.add_argument("-r", "--rate", type=float, default=48000.0, help="sample rate Hz (default: 48000)")
    p.add_argument("--fc", type=float, default=145.0e6, help="RF carrier Hz (default: 145 MHz, 2 m band)")
    p.add_argument("--speed", type=float, default=100.0, help="vehicle speed km/h (default: 100)")
    p.add_argument("--k", type=float, default=0.0, help="Rician K factor, 0 = Rayleigh (default: 0)")
    p.add_argument("--cnr", type=float, default=30.0, help="RF C/N in dB (default: 30)")
    p.add_argument("--deviation", type=float, default=3000.0, help="FM deviation Hz (default: 3000)")
    p.add_argument("--paths", type=int, default=50, help="multipath scatterer count (default: 50)")
    p.add_argument("--limiter", type=float, default=3.0,
                   help="discriminator output clamp in deviation units (default: 3)")
    p.add_argument("--seed", type=int, default=0, help="RNG seed (default: 0)")
    args = p.parse_args()

    audio = np.fromfile(args.infile, dtype=np.float32)
    out = mobile_channel(audio, args.rate, np.random.default_rng(args.seed),
                         args.fc, args.speed, args.k, args.cnr,
                         args.deviation, args.paths, args.limiter)
    out.tofile(args.out)
    fd = (args.speed / 3.6) / (3.0e8 / args.fc)
    print(f"mobile: {args.speed:g} km/h at {args.fc / 1e6:g} MHz "
          f"(fd={fd:.1f} Hz), K={args.k:g}, C/N={args.cnr:g} dB, "
          f"{args.paths} paths, seed={args.seed} -> {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
