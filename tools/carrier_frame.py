#!/usr/bin/env python3
"""Carrier-frame phase-error estimator: what does the receiver actually feel?

Background: PER is invariant to the in-frame noise-POWER difference between
channel arms that decode identically, so noise power is not the controlling
quantity the receiver experiences. The expert-prescribed instrument (the
TODOS.md "carrier-frame phase-error estimator" entry): form the normalized
error phasor against the clean carrier,

    u = z * conj(c) / |c|^2     (sample-wise, complex IF before the limiter)

and count, per keyed-frame sample: |dpsi| > pi excursions (whole phase
rotations — discriminator clicks) and the sub-2-pi |dpsi| > pi/2 tail
(large-but-not-rotating hits), plus the RMS phase error in the home region.
This tool runs the sweep, splits every trial's statistics by its decode
verdict (pass/fail through build/miniwolf_file, exact-TNC2-match scoring as
tools/ber.py), and reports which statistic separates the populations — the
controlling quantity is the one that does.

Pairing: the clean carrier c is the same channel chain with noise off under
the SAME seed (identical fading and keying; the noise draw in
tools/channel.py comes after the fading draws, so removing it does not move
the channel). The noisy z adds the noise draw as a separate stream, scaled
exactly as channel.mobile_channel does (unit-total-power IF-shaped complex
noise, referenced to the keyed-frame carrier).

Example:
    python3 tools/carrier_frame.py --cnr-min 5 --cnr-max 10 --trials 40
"""

import argparse
import concurrent.futures
import os
import re
import string
import subprocess
import sys
import tempfile

import numpy as np

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import channel
import frame as frame_mod

DECODE = re.compile(r"@ \d{2}:\d{2}:\d{2}\.\d{3} (.*)$", re.MULTILINE)


def random_text(rng: np.random.Generator, length: int) -> str:
    chars = np.frombuffer((string.ascii_letters + string.digits + " .,-/").encode("ascii"),
                          dtype=np.uint8)
    return bytes(rng.choice(chars, length)).decode("ascii")


def build_tx(args, text: str) -> np.ndarray:
    """The keyed audio, plus the silence margins, as frame.py builds it."""
    frame = frame_mod.ax25_frame(args.dest, args.source, [], text)
    bits = frame_mod.hdlc_bits(frame, args.head_flags, args.tail_flags)
    audio = frame_mod.modulate(bits, args.rate, args.baud, args.mark,
                               args.space, args.amplitude, args.phase_reset)
    pre = np.zeros(int(args.pre_silence * args.rate), dtype=np.float32)
    post = np.zeros(int(args.post_silence * args.rate), dtype=np.float32)
    return np.concatenate([pre, audio, post])


def if_pairs(audio, args):
    """(clean_if, noisy_if, keyed_mask): the complex IF with and without the
    noise draw, everything else identical (same seed -> same fading/keying).

    Mirrors channel.mobile_channel step for step; the noise is drawn from a
    separate stream and scaled by the same rule (unit-total-power IF-shaped
    complex noise, referenced to the keyed-frame carrier at the stated C/N).
    """
    rng = np.random.default_rng(args.seed)
    gate = channel.ptt_gate(audio, args.rate, args.mobile_ptt_gate)
    tx = channel.fm_modulate(audio, args.rate, args.mobile_deviation) * gate
    if args.mobile:
        fd = (args.mobile / 3.6) / (3.0e8 / args.mobile_fc)
        faded = tx * channel.clarke_channel(tx.size, args.rate, fd, rng,
                                            args.k, args.mobile_paths)
    else:
        fd = 0.0
        faded = tx
    keyed = faded[faded != 0]
    p_carrier = np.mean(np.abs(keyed) ** 2) if keyed.size else 1.0
    shape = channel.if_lowpass(
        rng.standard_normal(tx.size) + 1j * rng.standard_normal(tx.size),
        args.rate, args.mobile_if_bw)
    shape /= np.sqrt(np.mean(np.abs(shape) ** 2))
    sigma = np.sqrt(p_carrier / 10.0 ** (args.cnr / 10.0))
    return faded, faded + sigma * shape, gate > 0


def phase_stats(z: np.ndarray, c: np.ndarray, keyed: np.ndarray) -> dict:
    """The estimator on the keyed span: error phasor u = z*conj(c)/|c|^2,
    unwrapped phase error psi, per-sample increments dpsi."""
    ok = keyed & (np.abs(c) > 1e-12)
    u = (z[ok] * np.conj(c[ok])) / np.maximum(np.abs(c[ok]) ** 2, 1e-30)
    psi = np.unwrap(np.angle(u))
    dpsi = np.diff(psi)
    n = max(1, dpsi.size)
    rot = np.abs(dpsi) > np.pi                      # whole rotations
    tail = (np.abs(dpsi) > np.pi / 2) & ~rot        # sub-2-pi hits
    return {
        "rot_frac": float(np.count_nonzero(rot)) / n,
        "tail_frac": float(np.count_nonzero(tail)) / n,
        "psi_rms": float(np.sqrt(np.mean(psi ** 2))),
    }


def run_trial(job):
    """One (cnr, seed, text) point: stats on the IF pair + decode verdict."""
    args, cnr, seed, text = job
    args.cnr = cnr
    args.seed = seed
    audio = build_tx(args, text)
    clean, noisy, keyed = if_pairs(audio, args)
    stats = phase_stats(noisy, clean, keyed)
    rx = channel.fm_demodulate(
        channel.if_lowpass(noisy, args.rate, args.mobile_if_bw),
        args.rate, args.mobile_deviation, args.mobile_limiter)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "rx.f32")
        rx.tofile(path)
        cp = subprocess.run(
            [args.bin, "-f", path, "-r", str(args.rate)],
            capture_output=True, text=True, errors="replace")
    truth = frame_mod.tnc2(args.dest, args.source, [], text)
    ok = truth in DECODE.findall(cp.stderr)
    return ok, stats


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", default=os.path.join(ROOT, "build", "miniwolf_file"),
                   help="bench binary (default: build/miniwolf_file)")
    p.add_argument("--cnr-min", type=float, default=5.0, dest="cnr_min",
                   help="lowest C/N dB (default: 5)")
    p.add_argument("--cnr-max", type=float, default=10.0, dest="cnr_max",
                   help="highest C/N dB (default: 10)")
    p.add_argument("--cnr-step", type=float, default=1.0, dest="cnr_step",
                   help="C/N step dB (default: 1)")
    p.add_argument("--trials", type=int, default=40,
                   help="trials per C/N point (default: 40)")
    p.add_argument("--seed", type=int, default=1,
                   help="first seed; seeds are consecutive (default: 1)")
    p.add_argument("-r", "--rate", type=float, default=48000.0,
                   help="sample rate Hz (default: 48000)")
    p.add_argument("--text-len", type=int, default=30, dest="text_len",
                   help="random payload length (default: 30)")
    p.add_argument("--dest", default="APZMWL")
    p.add_argument("--source", default="N0CALL")
    p.add_argument("--head-flags", type=int, default=16)
    p.add_argument("--tail-flags", type=int, default=4)
    p.add_argument("--pre-silence", type=float, default=0.2)
    p.add_argument("--post-silence", type=float, default=0.2)
    p.add_argument("--baud", type=float, default=frame_mod.BAUD)
    p.add_argument("--mark", type=float, default=frame_mod.MARK_HZ)
    p.add_argument("--space", type=float, default=frame_mod.SPACE_HZ)
    p.add_argument("--amplitude", type=float, default=0.5)
    p.add_argument("--phase-reset", action="store_true")
    p.add_argument("--mobile", type=float, default=None, metavar="KPH",
                   help="add VHF picket fencing at this vehicle speed")
    p.add_argument("--k", type=float, default=0.0,
                   help="Rician K for --mobile (default: 0, Rayleigh)")
    p.add_argument("--mobile-fc", type=float, default=144.8e6)
    p.add_argument("--mobile-deviation", type=float, default=3000.0)
    p.add_argument("--mobile-paths", type=int, default=50)
    p.add_argument("--mobile-if-bw", type=float, default=25000.0)
    p.add_argument("--mobile-limiter", type=float, default=3.0)
    p.add_argument("--mobile-ptt-gate", type=float, default=0.0025)
    p.add_argument("--jobs", type=int, default=0,
                   help="parallel trials (default: all CPUs)")
    args = p.parse_args()
    if not os.access(args.bin, os.X_OK):
        print(f"carrier_frame.py: error: {args.bin} missing or not executable "
              "(build it first)", file=sys.stderr)
        return 1
    args.mobile_deviation = args.mobile_deviation  # single knob, kept explicit

    cnrs = np.arange(args.cnr_min, args.cnr_max + 1e-9, args.cnr_step)
    master = np.random.default_rng(args.seed)
    jobs, meta = [], []
    for cnr in cnrs:
        for t in range(args.trials):
            text = random_text(master, args.text_len)
            jobs.append((args, float(cnr), args.seed + t, text))
            meta.append((float(cnr), t))

    njobs = args.jobs or (len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity")
                          else os.cpu_count() or 1)
    with concurrent.futures.ThreadPoolExecutor(max_workers=njobs) as pool:
        results = list(pool.map(run_trial, jobs))

    print(f"{'C/N':>5} {'PER':>7} | {'pop':>4} {'rot/sample':>11} "
          f"{'tail/sample':>12} {'psi_rms':>8}   (median per pass/fail)")
    for cnr in cnrs:
        rows = [(ok, s) for (ok, s), (c, _) in zip(results, meta) if c == cnr]
        per = sum(not ok for ok, _ in rows) / len(rows)
        cells = []
        for pop, flag in (("pass", True), ("fail", False)):
            sel = [s for ok, s in rows if ok == flag]
            if sel:
                med = lambda k: float(np.median([s[k] for s in sel]))
                cells.append(f"{pop}={len(sel):3d} {med('rot_frac'):11.2e} "
                             f"{med('tail_frac'):12.2e} {med('psi_rms'):8.3f}")
            else:
                cells.append(f"{pop}=  0  (none)")
        print(f"{cnr:5.1f} {per:7.1%} | " + "  |  ".join(cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())
