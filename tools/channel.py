#!/usr/bin/env python3
"""The radio channel, as a transceiver sees it: Bell202 audio in, what the
soundcard would deliver out.

One module, one definition each of the channel arms, so generator CLI and
sweep tool cannot drift apart. Noise is thermal, added at RF, and
characterized as C/N in dB at the IF filter's output — exactly where a
transceiver lives; no audio-SNR reference exists in the tooling. The arms:

- mobile_channel() — the whole FM link: NFM modulate, Clarke/Rician
  multipath (picket fencing; Rician K for open-road LOS), thermal noise at
  C/N, IF channel filter, limiter discriminator. The receiver is stationary;
  everything that fades is the distant station's signal.
- cfo_offset() — the AFSK tone pair shifted off its nominal 1200/2200 Hz by
  a constant offset (off-tune receiver / mis-centered transmitter).

The TX clock error is not a channel function: the transmitter's baud rate
simply IS what frame.py's --baud sets (1200.3 instead of 1200 — the clock
error), so the generator models it at the source.

Standalone use:
    python3 tools/frame.py --text hi -o clean.f32
    python3 tools/channel.py -i clean.f32 -o rx.f32 --speed 90 --cnr 25
    build/miniwolf_file -f rx.f32 -r 48000
"""

import argparse
import os
import sys

import numpy as np


def cfo_offset(x: np.ndarray, rate: float, hz: float) -> np.ndarray:
    """Frequency offset: real carrier multiply by cos(2*pi*hz*t).

    A real translation — the tone pair moves together (1200->1200+hz,
    2200->2200+hz) and an image term appears at the mirror frequencies,
    which is what an off-tune receiver plus its audio band actually sees.
    """
    t = np.arange(x.size, dtype=np.float64) / rate
    return np.asarray(x * np.cos(2.0 * np.pi * hz * t), dtype=np.float32)


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


def ptt_gate(audio: np.ndarray, rate: float, window_s: float = 0.0025) -> np.ndarray:
    """Transmit keying mask: the FM transmitter is on the air only where the
    audio is non-silent (rolling max envelope over window_s — an AFSK frame
    never stays at zero that long; inter-frame silence does). Without this,
    FM-modulating digital silence emits a constant full-power carrier, and
    the gaps between frames would fade like a signal — picket fencing where
    a real station has unkeyed and the receiver hears only its noise floor."""
    block = max(1, int(window_s * rate))
    n_blocks = (audio.size + block - 1) // block
    padded = np.pad(np.abs(audio), (0, n_blocks * block - audio.size))
    env = padded.reshape(n_blocks, block).max(axis=1)
    return (np.repeat(env, block)[:audio.size] > 1e-9).astype(np.float64)


def if_lowpass(iq: np.ndarray, rate: float, bw: float) -> np.ndarray:
    """IF channel filter, complex baseband: windowed-sinc lowpass at bw/2
    (bw = the channel bandwidth, 12.5/20/25 kHz for NFM). Without it, the
    added noise is white across the whole Nyquist band, adjacent samples are
    uncorrelated, and the discriminator sees near-uniform +-pi phase steps —
    a full-scale hiss no real radio produces, because its IF filter has
    already band-limited the noise before the limiter. Group delay of the
    linear-phase FIR delays the whole file equally, which the demodulator
    does not care about."""
    if not bw or bw > 0 and bw >= rate:  # cutoff bw/2 already at Nyquist
        return iq
    taps = 129
    fc = bw / 2.0 / rate
    n = np.arange(taps) - (taps - 1) / 2
    h = np.sinc(2 * fc * n) * np.hamming(taps)
    h /= h.sum()
    return np.convolve(iq, h, mode="same")


def mobile_channel(audio: np.ndarray, rate: float, rng: np.random.Generator,
                   fc: float = 144.8e6, speed_kph: float = 90.0, k: float = 0.0,
                   cnr_db: float | None = 30.0, deviation: float = 3000.0,
                   paths: int = 50, limiter: float = 3.0,
                   ptt_window_s: float = 0.0025,
                   if_bw: float = 25000.0) -> np.ndarray:
    """The whole mobile link: FM modulate -> multipath fade -> thermal noise
    at the RF C/N (defined in the IF bandwidth, as radio specs define it) ->
    IF channel filter -> limiter discriminator. fd from physical speed at
    the carrier (90 km/h at 144.8 MHz is ~12.1 Hz Doppler spread).
    cnr_db=None skips the RF noise (pure fading) — for stacking further
    impairments on top, e.g. audio AWGN after the discriminator. The TX is
    keyed only where audio is present (ptt_window_s=0 keeps the carrier
    permanently on — useful for tone tests, wrong for inter-frame gaps).
    if_bw is the IF/channel bandwidth (12.5/20/25 kHz NFM; 25 kHz wide NBFM
    is amateur VHF simplex) — 0 or None disables the filter."""
    c = 3.0e8
    fd = (speed_kph / 3.6) / (c / fc)
    gate = None if ptt_window_s <= 0 else ptt_gate(audio, rate, ptt_window_s)
    tx = fm_modulate(audio, rate, deviation)
    if gate is not None:
        tx = tx * gate
        # C/N references the keyed carrier; with nothing keyed on at all
        # there is no carrier to reference — fall back to a unit carrier
        # so a fully-silent input still yields calibrated noise-only hiss
        keyed = tx[gate > 0]
        noise_ref = keyed if keyed.size else np.ones(1)
    else:
        noise_ref = tx
    faded = tx if fd == 0.0 else tx * clarke_channel(tx.size, rate, fd, rng, k, paths)
    rx = faded
    if cnr_db is not None:
        cnr = 10.0 ** (cnr_db / 10.0)
        # unit-power noise in the IF band: filter first, renormalize, then
        # scale; rho = keyed-carrier power / E|n|^2, total complex power
        keyed = noise_ref[noise_ref != 0]
        p_carrier = np.mean(np.abs(keyed) ** 2) if keyed.size else 1.0
        shape = if_lowpass(rng.standard_normal(tx.size)
                           + 1j * rng.standard_normal(tx.size),
                           rate, if_bw)
        shape /= np.sqrt(np.mean(np.abs(shape) ** 2))
        sigma = np.sqrt(p_carrier / cnr)
        rx = faded + sigma * shape
    return fm_demodulate(if_lowpass(rx, rate, if_bw), rate, deviation, limiter)


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-i", "--in", dest="infile", required=True, help="input raw f32le audio file")
    p.add_argument("-o", "--out", default="mobile.f32", help="output raw f32le file (default: mobile.f32)")
    p.add_argument("-r", "--rate", type=float, default=48000.0, help="sample rate Hz (default: 48000)")
    p.add_argument("--fc", type=float, default=144.8e6, help="RF carrier Hz (default: 144.8 MHz, NA APRS)")
    p.add_argument("--speed", type=float, default=90.0, help="vehicle speed km/h (default: 90)")
    p.add_argument("--k", type=float, default=0.0, help="Rician K factor, 0 = Rayleigh (default: 0)")
    p.add_argument("--cnr", type=float, default=30.0, help="RF C/N in dB (default: 30)")
    p.add_argument("--deviation", type=float, default=3000.0, help="FM deviation Hz (default: 3000)")
    p.add_argument("--paths", type=int, default=50, help="multipath scatterer count (default: 50)")
    p.add_argument("--limiter", type=float, default=3.0,
                   help="discriminator output clamp in deviation units (default: 3)")
    p.add_argument("--if-bw", type=float, default=25000.0, metavar="HZ",
                   help="IF/channel bandwidth Hz before the limiter; "
                        "12.5/20/25 kHz NFM channels, 0 disables (default: "
                        "25000, wide NBFM amateur simplex)")
    p.add_argument("--ptt-gate", type=float, default=0.0025, metavar="S",
                   help="TX keying envelope window in seconds; 0 keeps the "
                        "carrier permanently on (default: 0.0025)")
    p.add_argument("--seed", type=int, default=0, help="RNG seed (default: 0)")
    args = p.parse_args()

    audio = np.fromfile(args.infile, dtype=np.float32)
    out = mobile_channel(audio, args.rate, np.random.default_rng(args.seed),
                         args.fc, args.speed, args.k, args.cnr,
                         args.deviation, args.paths, args.limiter,
                         args.ptt_gate, args.if_bw)
    out.tofile(args.out)
    fd = (args.speed / 3.6) / (3.0e8 / args.fc)
    print(f"mobile: {args.speed:g} km/h at {args.fc / 1e6:g} MHz "
          f"(fd={fd:.1f} Hz), K={args.k:g}, C/N={args.cnr:g} dB, "
          f"{args.paths} paths, seed={args.seed} -> {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
