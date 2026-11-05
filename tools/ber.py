#!/usr/bin/env python3
"""Monte Carlo end-to-end PER sweeps for the miniwolf bench chain.

By default each trial is AWGN-degraded at the audio SNR given on the
ladder (--snr-min/--snr-max/--snr-step). The impairment arms change the
channel per trial instead: --cfo/--ppm/--delay/--burst degrade the audio
alongside the AWGN (tools/impairments.py), and --mobile replaces AWGN with
the VHF mobile fading channel (tools/channel_mobile.py) — in which case the
ladder is reinterpreted as RF C/N in dB, not audio SNR.

References (in-repo):
- src/main_bench.c — the "N @ HH:MM:SS.mmm TNC2" decode printout (stderr)
- tools/frame.py — the Bell202 generator
- tools/impairments.py — the degradation arms
- tools/channel_mobile.py — the VHF mobile link

Ported from rxx_lora's tools/ber.py: same paired-seed contract (the whole
trial matrix is drawn up front from one --seed, so two runs that differ only
in receiver flags align trial for trial under --verdicts), same table shape
on stdout for tools/ber_combine.py. The score is frame-level, not bit-level:
an AX.25 frame either survives its HDLC FCS check (exact TNC2 match against
ground truth) or the whole packet is lost — PER = frames not correctly
decoded. There is no partial credit; miniwolf emits hard bits only.
"""

import argparse
import concurrent.futures
import os
import re
import shlex
import string
import subprocess
import sys
import tempfile
import time

import numpy as np

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import frame as frame_mod
import impairments
import channel_mobile

# main_bench.c decode line: "i |  main | 1 @ 00:00:01.234 N0CALL>APZMWL:text"
DECODE = re.compile(r"@ \d{2}:\d{2}:\d{2}\.\d{3} (.*)$", re.MULTILINE)


def max_jobs() -> int:
    try:
        return len(os.sched_getaffinity(0))  # truth under cgroup/taskset limits
    except AttributeError:  # not Linux
        return os.cpu_count() or 1


def random_text(rng: np.random.Generator, length: int) -> str:
    """Random printable ASCII payload (no newline: TNC2 is one line)."""
    chars = np.frombuffer((string.ascii_letters + string.digits + " .,-/").encode("ascii"),
                          dtype=np.uint8)
    return bytes(rng.choice(chars, length)).decode("ascii")


def run_trial(args, truth: str, text: str, snr: float, seed: int, tmp: str):
    """Wire one trial: generate, noise, decode, score.

    Returns (ok: bool, outcome: str, ndecodes: int). outcome is ok/wrong/
    none: wrong = packets came out of the FCS check but none matches ground
    truth (false decodes), none = silence.
    """
    frame = frame_mod.ax25_frame(args.dest, args.source, [], text)
    bits = frame_mod.hdlc_bits(frame, args.head_flags, args.tail_flags)
    audio = frame_mod.modulate(bits, args.rate, args.baud, args.mark,
                               args.space, args.amplitude, args.phase_reset)
    pre = np.zeros(int(args.pre_silence * args.rate), dtype=np.float32)
    post = np.zeros(int(args.post_silence * args.rate), dtype=np.float32)
    audio = np.concatenate([pre, audio, post])
    # same arm order as frame.py's main(): channel first, noise last
    rng = np.random.default_rng(seed)
    if getattr(args, "burst", None) is not None:
        audio = impairments.squelch_burst(audio, args.rate, rng, args.burst)
    if getattr(args, "cfo", None) is not None:
        audio = impairments.cfo_offset(audio, args.rate, args.cfo)
    if getattr(args, "ppm", None) is not None:
        audio = impairments.ppm_drift(audio, args.ppm)
    if getattr(args, "delay", None) is not None:
        audio = impairments.frac_delay(audio, args.delay)
    if getattr(args, "mobile", None) is not None:
        # the mobile arm's noise reference is RF C/N at the FM demodulator
        # input, not audio SNR (see tools/channel_mobile.py). By default the
        # ladder IS the C/N; --mobile-cnr pins it instead, and the ladder
        # reverts to audio SNR, stacked as AWGN after the discriminator
        pin = getattr(args, "mobile_cnr", None)
        audio = channel_mobile.mobile_channel(
            audio, args.rate, rng, fc=args.mobile_fc, speed_kph=args.mobile,
            k=args.k, cnr_db=pin if pin is not None else snr,
            deviation=args.mobile_deviation, paths=args.mobile_paths,
            limiter=args.mobile_limiter)
        if pin is not None:
            audio = impairments.add_awgn(audio, snr, rng)
    else:
        audio = impairments.add_awgn(audio, snr, rng)
    noisy = os.path.join(tmp, "noisy.f32")
    audio.tofile(noisy)
    cp = subprocess.run(
        [args.bin, "-f", noisy, "-r", str(args.rate), *args.bench_argv],
        capture_output=True, text=True, errors="replace")
    got = DECODE.findall(cp.stderr)
    if truth in got:
        return True, "ok", len(got)
    return False, ("wrong" if got else "none"), len(got)


def run_trial_task(job):
    """Pool worker: one trial, in a temp dir of its own. A trial is fully
    described by the draws main() already made, so fan-out cannot move a
    result."""
    args, truth, text, snr, seed = job
    with tempfile.TemporaryDirectory() as tmp:
        return run_trial(args, truth, text, snr, seed, tmp)


def selftest(args) -> bool:
    """One fixed trial at a comfortable level: the frame must decode. The
    mobile arm gets 40 dB C/N (Rayleigh at speed dies far above the AWGN
    arm's comfortable 20 dB — that is the channel, not the pipeline)."""
    snr = 40 if args.mobile is not None else 20
    with tempfile.TemporaryDirectory() as tmp:
        ok, outcome, n = run_trial(args, frame_mod.tnc2(
            args.dest, args.source, [], "Hello miniwolf"),
            "Hello miniwolf", snr, 1, tmp)
        print(f"selftest: {'PASS' if ok else 'FAIL'}  (snr={snr}: outcome={outcome}, "
              f"{n} decode(s))")
        return ok


def main() -> None:
    p = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__.split("\n\n")[0],
        epilog=(
            "Per SNR point, how many of the generated packets the bench "
            "binary decodes back. Each trial builds a frame (frame.py "
            "functions), noises it at the audio SNR (frame.add_awgn), and "
            "hands the file to the shipped binary — exactly as a human "
            "would. A lost frame is one whose TNC2 line never appears (HDLC "
            "FCS caught it) or came out corrupted.\n"
            "Impairments: --cfo/--ppm/--delay/--burst add fixed channel "
            "damage on top of the AWGN; --mobile swaps the whole channel for "
            "the VHF mobile fading link (then the ladder means RF C/N, and "
            "--mobile-fc/--mobile-deviation/--mobile-paths/--mobile-limiter "
            "shape the link).\n"
            "Paired A/B: two runs sharing --seed draw identical frames and "
            "noise, so a demod change can be attributed trial by trial.\n"
            "AWGN is applied over the whole file including the silence "
            "padding, diluting the applied SNR by a fraction of a dB — an "
            "accepted bias."
        ))
    p.add_argument("--snr-min", type=float, default=-14, help="first SNR point [dB]")
    p.add_argument("--snr-max", type=float, default=2, help="last SNR point [dB]")
    p.add_argument("--snr-step", type=float, default=2, help="SNR step [dB]")
    p.add_argument("--trials", type=int, default=50, help="noise realizations per SNR point")
    p.add_argument("--seed", type=int, default=1, help="RNG seed for text and noise")
    p.add_argument("--text-len", type=int, default=30,
                   help="payload length in characters (default: 30)")
    p.add_argument("--jobs", type=int, default=max_jobs(),
                   help="worker processes for the trial matrix (1 = serial; "
                        "the draws, the summary and --verdicts lines are "
                        "identical either way)")
    p.add_argument("--bin", default=os.path.join(ROOT, "build", "mw_bench"),
                   help="path to the bench binary under test")
    p.add_argument("--bench-args", default="", metavar="ARGS",
                   help="extra arguments for the binary, split like a shell "
                        "command line (e.g. '--squelch 0.5'). The draws are "
                        "unaffected, so two runs differing only here align "
                        "line for line under --verdicts: a knob, A/B'd")
    # frame.py passthrough (kept in sync with tools/frame.py's options)
    p.add_argument("-r", "--rate", type=float, default=48000.0)
    p.add_argument("--baud", type=float, default=frame_mod.BAUD)
    p.add_argument("--mark", type=float, default=frame_mod.MARK_HZ)
    p.add_argument("--space", type=float, default=frame_mod.SPACE_HZ)
    p.add_argument("--amplitude", type=float, default=0.5)
    p.add_argument("--head-flags", type=int, default=16)
    p.add_argument("--tail-flags", type=int, default=4)
    p.add_argument("--pre-silence", type=float, default=0.2)
    p.add_argument("--post-silence", type=float, default=0.2)
    p.add_argument("--phase-reset", action="store_true")
    p.add_argument("--dest", default="APZMWL")
    p.add_argument("--source", default="N0CALL")
    # impairment arms (kept in sync with tools/frame.py / tools/impairments.py)
    p.add_argument("--cfo", type=float, default=None, metavar="HZ",
                   help="fixed frequency offset in Hz for every trial")
    p.add_argument("--ppm", type=float, default=None, metavar="PPM",
                   help="fixed sample-rate error in ppm for every trial")
    p.add_argument("--delay", type=float, default=None, metavar="SAMPLES",
                   help="fixed fractional sample delay for every trial")
    p.add_argument("--burst", type=float, default=None, metavar="S",
                   help="one squelch burst of this many seconds per trial, "
                        "at a seed-drawn position")
    p.add_argument("--mobile", type=float, default=None, metavar="KPH",
                   help="VHF mobile fading channel (tools/channel_mobile.py) "
                        "instead of AWGN: vehicle speed; the SNR ladder is "
                        "reinterpreted as RF C/N in dB")
    p.add_argument("--k", type=float, default=0.0,
                   help="Rician K factor for --mobile, 0 = Rayleigh (default: 0)")
    p.add_argument("--mobile-fc", type=float, default=145.0e6, dest="mobile_fc",
                   help="RF carrier Hz for --mobile (default: 145 MHz, 2 m band)")
    p.add_argument("--mobile-deviation", type=float, default=3000.0,
                   dest="mobile_deviation", help="FM deviation Hz for --mobile (default: 3000)")
    p.add_argument("--mobile-paths", type=int, default=50, dest="mobile_paths",
                   help="multipath scatterer count for --mobile (default: 50)")
    p.add_argument("--mobile-limiter", type=float, default=3.0,
                   dest="mobile_limiter",
                   help="discriminator clamp in deviation units for --mobile (default: 3)")
    p.add_argument("--mobile-cnr", type=float, default=None, dest="mobile_cnr",
                   help="pin the mobile link's RF C/N in dB and stack AWGN "
                        "at the ladder SNR on top. Without it, the ladder "
                        "itself is the RF C/N (no AWGN stage)")
    p.add_argument("--verdicts", default=None, metavar="PATH",
                   help="write one line per trial (inputs + verdict), so a "
                        "two-binary A/B attribution is a plain `diff` of two "
                        "files: the same --seed draws the same trials in both "
                        "runs, so the lines align and only the verdicts differ")
    p.add_argument("--out", default="ber.png", help="output PNG path")
    p.add_argument("--selftest", action="store_true",
                   help="one fixed trial at snr=20, exit 1 on mismatch")
    args = p.parse_args()

    if not os.access(args.bin, os.X_OK):
        p.error(f"{args.bin} missing or not executable (build it first)")
    if args.text_len < 1:
        p.error("--text-len must be >= 1")
    args.bench_argv = shlex.split(args.bench_args)

    if args.selftest:
        sys.exit(0 if selftest(args) else 1)

    snrs = np.arange(args.snr_min, args.snr_max + 1e-9, args.snr_step)
    rng = np.random.default_rng(args.seed)
    arms = " ".join(f"{k}={v}" for k, v in (("cfo", args.cfo), ("ppm", args.ppm),
                                            ("delay", args.delay), ("burst", args.burst),
                                            ("mobile", args.mobile), ("K", args.k),
                                            ("fc", args.mobile_fc if args.mobile is not None else None),
                                            ("dev", args.mobile_deviation if args.mobile is not None else None),
                                            ("cnr", args.mobile_cnr))
                    if v is not None) or "clean"
    args.arms = arms
    print(f"PER over AWGN  audio {args.rate:g} Hz  text {args.text_len} chars  "
          f"{args.trials} trials/point  seed={args.seed}  arms={arms}", flush=True)
    print(" SNR [dB]        PER      ok  wrong  none  extra", flush=True)
    # Draw the whole matrix up front, in serial-loop order: the --verdicts
    # draw sequence is part of the A/B contract and must not move.
    draws = []
    for snr in snrs:
        for ti in range(args.trials):
            text = random_text(rng, args.text_len)
            awgn_seed = int(rng.integers(1 << 31))
            draws.append((float(snr), ti, text, awgn_seed))

    truth = frame_mod.tnc2(args.dest, args.source, [], "")
    jobs = [(args, truth + t, t, sn, sd) for sn, _, t, sd in draws]
    pers = []
    verdicts = []
    by_snr = {float(s): {"ok": 0, "wrong": 0, "none": 0, "extra": 0}
              for s in snrs}
    # progress to stderr; stdout stays exactly the final table (ber_combine
    # parses these logs)
    tick = 100
    t0 = time.perf_counter()
    done = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for (snr, ti, _, seed), (ok, outcome, ndecodes) in zip(
                draws, ex.map(run_trial_task, jobs, chunksize=4)):
            acc = by_snr[snr]
            acc[outcome] += 1
            # decodes beyond the one expected packet: false decodes out of
            # noise or FCS-collision garbage
            acc["extra"] += max(0, ndecodes - 1)
            verdicts.append(
                f"snr={snr:g} t={ti} seed={seed} outcome={outcome} "
                f"decodes={ndecodes}")
            done += 1
            if done % tick and ti != args.trials - 1:
                continue
            el = time.perf_counter() - t0
            print(f"  [{el:6.1f} s] {done:6d}/{len(draws)} trials   "
                  f"{done / el:6.1f} trials/s", file=sys.stderr, flush=True)
            if ti != args.trials - 1:  # point still running: no row yet
                continue
            per = (args.trials - acc["ok"]) / args.trials
            pers.append(per)
            print(f"{snr:7.1f}   {per:.3e}   {acc['ok']:4d}  "
                  f"{acc['wrong']:4d}  {acc['none']:4d}  {acc['extra']:5d}",
                  flush=True)
    if args.verdicts:
        with open(args.verdicts, "w") as vh:
            vh.write("\n".join(verdicts) + "\n")

    plot_per(args, snrs, pers)


def plot_per(args, snrs, pers) -> None:
    """PER curve, written to args.out (matplotlib imported lazily: never
    paid by --selftest or a crashed sweep)."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(9, 6), constrained_layout=True)
    # a log axis cannot show zero PER; NaN breaks the line there instead
    ax.semilogy(
        snrs,
        np.where(np.asarray(pers) > 0, pers, np.nan),
        "o-",
        label="mw_bench end to end (0 errors: no point)",
    )
    ax.set_xlim(float(snrs[0]), float(snrs[-1]))
    ax.set_ylim(1 / (10 * args.trials), 1.1)
    ax.set_xlabel("RF C/N [dB]" if getattr(args, "mobile", None) is not None
                  else "audio SNR [dB]")
    ax.set_ylabel("packet error rate")
    fig.suptitle("miniwolf_bench end-to-end PER over AWGN", fontsize=13)
    ax.set_title(
        f"{args.rate:g} Hz, text {args.text_len} chars, {args.trials} "
        f"trials/point, seed={args.seed}, {getattr(args, 'arms', 'clean')}",
        fontsize=9)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
