#!/usr/bin/env python3
"""End-to-end regression suite: PASS/FAIL matrix over the decode paths that must never regress.

Synthetic-first (no capture material needed): every case is a tools/frame.py
variant — clean, digi path, min/max frame, multi-frame, --phase-reset, clock
error, rate arm, and one deterministic impairment arm per existing channel
feature — decoded through the miniwolf_file entry point exactly as a human
would, and compared TNC2 line for line against frame.py's ground truth (same
contract as tools/ber.py: a frame either comes out byte-exact or it is lost).
On top sit the WA8LMF tracks in data/ with their measured decode-count floors
(data/README.md holds the measurements); tracks absent from the checkout are
SKIP, not FAIL — the suite must run anywhere.

stdout is the PASS/FAIL matrix, one line per case; the exit code is non-zero
if any case FAILED. tools/regression_perf.py is the speed twin: same cases,
same verdicts, plus a real-time multiplier per line — the verdicts gate the
exit code, never the speed.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)

# main_file.c decode line: "i |  main | 1 @ 00:00:01.234 N0CALL>APZMWL:text"
DECODE = re.compile(r"@ \d{2}:\d{2}:\d{2}\.\d{3} (.*)$", re.MULTILINE)
PACKETS = re.compile(r"^.*Packets: (\d+)$", re.MULTILINE)
MODEM_TIME = re.compile(r"Time in modem_demodulate: ([0-9.]+) s")

# (name, frame.py argv) — xfail=True marks a known current gap that must not
# gate the tree: it shows as XFAIL while failing and XPASS the day it decodes.
# Measured boundaries (2026-11): a 1-char payload never decodes (2 chars do);
# 200 Hz tone offset never decodes (150 Hz does — see the DC offset /
# frequency-error tolerance entry in TODOS.md).
MAX_TEXT = ("The quick brown fox jumps over the lazy dog 0123456789. " * 4).strip()
CASES = [
    ("clean", [], False),
    ("digi-path", ["--digi", "WIDE1-1", "--digi", "WIDE2-1"], False),
    ("min-frame", ["--text", "x"], True),
    ("max-frame", ["--text", MAX_TEXT], False),
    ("multi-3", ["--frames", "3"], False),
    ("phase-reset", ["--phase-reset"], False),
    ("rate-22050", ["-r", "22050"], False),
    ("baud-1200.3", ["--baud", "1200.3"], False),
    ("cnr-12-static", ["--cnr", "12", "--seed", "1"], False),
    ("cfo-200", ["--cfo", "200"], True),
]

# WA8LMF bench tracks (gitignored): measured decode-count floors.
# track01 measured 1010, track02 measured 934 (data/README.md); the floors
# sit ~1% under the measurements — regression tripwires, not exact pins.
# Tracks 3+ are secondary and deliberately not gated.
TNCTESTS = [
    ("track01", os.path.join(ROOT, "data", "tnctest01_22050Hz_mono_S16LE.raw"),
     "-F S16 -r 22050", 1000),
    ("track02", os.path.join(ROOT, "data", "tnctest02_22050Hz_mono_S16LE.raw"),
     "-F S16 -r 22050", 925),
]


def case_rate(argv: list[str]) -> int:
    """Sample rate a case's frame.py argv transmits at (frame.py's default)."""
    for a, b in zip(argv, argv[1:]):
        if a == "-r":
            return int(float(b))
    return 48000


def decode(args, audio_opts: str) -> tuple[list[str], str]:
    """Run the bench binary on one audio file; return (TNC2 decodes, stderr).

    audio_opts carries the -f FILE plus every rate/format flag verbatim."""
    cmd = f"{args.bin} {audio_opts}".split()
    cp = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if cp.returncode != 0:
        return [], cp.stderr
    return DECODE.findall(cp.stderr), cp.stderr


def run_case(args, name: str, argv: list[str], xfail: bool, tmp: str) -> tuple[str, str, str]:
    """One synthetic case: generate with frame.py, decode, compare to truth.

    Returns (verdict, detail, stderr). verdict is PASS/FAIL/XFAIL/XPASS/ERROR;
    only FAIL (and ERROR) gate."""
    audio = os.path.join(tmp, f"{name}.f32")
    gen = subprocess.run([sys.executable, args.frame_py, *argv, "-o", audio],
                         capture_output=True, text=True)
    if gen.returncode != 0 or not os.path.exists(audio):
        return "ERROR", f"frame.py rc={gen.returncode}", gen.stderr[-400:]
    truth = gen.stdout.splitlines()
    got, stderr = decode(args, f"-f {audio} -r {case_rate(argv)}")
    if got == truth:
        return "XPASS" if xfail else "PASS", f"{len(got)}/{len(truth)} frames", stderr
    return "XFAIL" if xfail else "FAIL", f"{len(got)}/{len(truth)} decodes match", stderr


def run_track(args, name: str, path: str, opts: str, floor: int, tmp: str) -> tuple[str, str, str]:
    """One WA8LMF track: decode, count packets, require >= floor."""
    got, stderr = decode(args, f"-f {path} {opts}")
    if not got and "Cannot open file" in stderr:
        return "SKIP", "capture material not in checkout", stderr
    m = PACKETS.search(stderr)
    n = int(m.group(1)) if m else len(got)
    if n >= floor:
        return "PASS", f"{n} >= {floor}", stderr
    return "FAIL", f"{n} < {floor}", stderr


def audio_seconds(path: str, rate: int, bytes_per_sample: int) -> float:
    """Audio duration from the file size (raw mono PCM) and its rate."""
    return os.path.getsize(path) / bytes_per_sample / rate if rate else 0.0


def fmt_bytes_per_sample(opts: str) -> int:
    """Bytes per sample from a -F FORMAT flag in the decode options."""
    for a, b in zip(opts.split(), opts.split()[1:]):
        if a == "-F":
            return int(b[1:]) // 8
    return 4  # the synthetic cases write f32le


def run_all(args) -> int:
    failures = 0
    print(f"{'case':<16}{'verdict':<9}{'detail':<24}"
          + ("modem s    xRT" if args.perf else ""))
    with tempfile.TemporaryDirectory() as tmp:
        rows = []
        for name, argv, xfail in CASES:
            verdict, detail, stderr = run_case(args, name, argv, xfail, tmp)
            rows.append((name, verdict, detail, stderr, None, None, ""))
        for name, path, opts, floor in TNCTESTS:
            rate = int(opts.split("-r ")[1].split()[0])
            verdict, detail, stderr = run_track(args, name, path, opts, floor, tmp)
            rows.append((name, verdict, detail, stderr, path, rate, opts))

        for name, verdict, detail, stderr, path, rate, opts in rows:
            if verdict == "FAIL":
                failures += 1
            perf = ""
            if args.perf:
                m = MODEM_TIME.search(stderr)
                if m and path:
                    secs = float(m.group(1))
                    xrt = audio_seconds(path, rate, fmt_bytes_per_sample(opts)) / secs
                    perf = f"{secs:8.2f}  {xrt:5.2f}"
                elif m:
                    perf = f"{float(m.group(1)):8.2f}       -"
            print(f"{name:<16}{verdict:<9}{detail:<24}{perf}")
    print(f"{'-' * 49}")
    print(f"{len(rows)} cases, {failures} failed")
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", default=os.path.join(ROOT, "build", "miniwolf_file"),
                   help="bench binary under test (default: build/miniwolf_file)")
    p.add_argument("--frame-py", default=os.path.join(TOOLS, "frame.py"),
                   help="frame generator (default: tools/frame.py)")
    p.add_argument("--perf", action="store_true",
                   help="add the speed columns (modem seconds, xRT); "
                        "verdicts still gate the exit code")
    args = p.parse_args(argv)
    if not os.access(args.bin, os.X_OK):
        p.error(f"{args.bin} missing or not executable (build it first)")
    return run_all(args)


if __name__ == "__main__":
    sys.exit(main())
