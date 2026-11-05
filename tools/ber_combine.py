#!/usr/bin/env python3
"""Overlay several ber.py sweeps on one PER-vs-audio-SNR plot."""

import argparse
import os
import re
import sys

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ber.py's per-point row: snr, per, ok, wrong, none, extra
ROW = re.compile(
    r"^\s*(-?[\d.]+)\s+([\d.e+-]+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$")
# ber.py's header line: "PER over AWGN audio 48000 Hz text 30 chars 50
# trials/point seed=1"
HEAD = re.compile(
    r"PER over AWGN\s+audio\s+([\d.]+)\s+Hz\s+text\s+(\d+)\s+chars\s+"
    r"(\d+) trials/point\s+seed=(\d+)(?:\s+arms=(.*?))?\s*$")
COLORS = ["tab:blue", "tab:orange", "tab:green", "tab:red", "tab:purple", "tab:brown"]


def parse(path):
    """One sweep log -> header dict + per-point arrays."""
    head = None
    rows = []
    with open(path) as fh:
        for line in fh:
            if head is None:
                m = HEAD.search(line)
                if m:
                    head = m.groups()
                continue
            if ROW.match(line):
                rows.append([float(x) for x in ROW.match(line).groups()])
    if head is None:
        raise SystemExit(f"{path}: no ber.py header line (is this a sweep log?)")
    if not rows:
        raise SystemExit(f"{path}: no data rows")
    rate, text_len, trials, seed, arms = head
    arms = (arms or "clean").strip()
    a = np.array(rows)
    return {
        "rate": float(rate),
        "text_len": int(text_len),
        "trials": int(trials),
        "seed": int(seed),
        "arms": arms,
        "snr": a[:, 0],
        "per": a[:, 1],
        "wrong": a[:, 3],
        "none": a[:, 4],
    }


def curve(ax, d, color, label, lw=None):
    """One sweep; NaN out the zero-PER points, ring the false-decode ones."""
    per = d["per"]
    zero = per <= 0
    y = np.where(zero, np.nan, per)
    wrong = (d["wrong"] > 0) & ~zero
    ax.semilogy(d["snr"], y, "--", color=color, marker="s", label=label,
                linewidth=lw)
    if wrong.any():
        ax.semilogy(
            d["snr"][wrong], y[wrong], "s", color=color,
            markerfacecolor="none", markersize=11,
            markeredgewidth=1.5 if lw is None else 1.0)
    return per[~zero]


def main():
    p = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__.split("\n\n")[0],
        epilog=(
            "Reads the stdout tables of N saved ber.py sweeps and draws them "
            "together, one curve each, colored by log file (the usual use is "
            "a before/after of one configuration).\n"
            "examples:\n"
            "  python3 tools/ber.py --trials 200 --out quad.png | tee quad.log\n"
            "  python3 tools/ber_combine.py --out combined.png quad.log goertzel.log"
        ))
    p.add_argument("logs", nargs="+", help="ber.py sweep logs (stdout, tee'd or >)")
    p.add_argument("--out", default="ber_combined.png", help="output PNG path")
    args = p.parse_args()

    sweeps = [parse(path) for path in args.logs]
    for key in ("rate", "text_len", "trials"):
        vals = {s[key] for s in sweeps}
        if len(vals) > 1:
            print(f"warning: sweeps differ in {key} ({sorted(map(str, vals))}) — "
                  "curves may not be comparable", file=sys.stderr)

    fig, ax = plt.subplots(figsize=(9.5, 6.5), constrained_layout=True)
    many = len(sweeps) > len(COLORS)
    colors = plt.cm.viridis(np.linspace(0, 0.95, len(sweeps))) if many else COLORS
    lw = 1.0 if many else None
    lows, ends = [], []
    for i, (d, path) in enumerate(zip(sweeps, args.logs)):
        tag = os.path.splitext(os.path.basename(path))[0]
        lows.append(curve(ax, d, colors[i],
                          f"{tag}: {d['arms']}" if d["arms"] != "clean" else tag, lw))
        ends.append(d["snr"][d["per"] > 0])

    d0 = sweeps[0]
    ax.set_xlabel("audio SNR [dB]")
    ax.set_ylabel("packet error rate")
    ax.set_title(
        f"miniwolf_bench end-to-end PER over AWGN — {d0['rate']:g} Hz, "
        f"{d0['text_len']} chars, {d0['trials']} trials/point, seed="
        f"{d0['seed']}\n"
        "open markers = SNR points with false decodes; "
        "curves stop at the last point with an observed error", fontsize=10)
    # axes fit the plotted points, not the swept range: past the last
    # observed error there is nothing to show but empty space
    lo = min(s["snr"].min() for s in sweeps)
    # a sweep with no errors anywhere contributes no drawn point: fall back
    # to its full swept range so the limits stay finite
    hi = max((e[-1] if len(e) else s["snr"].max()) for e, s in zip(ends, sweeps))
    pad = 0.04 * (hi - lo)
    ax.set_xlim(lo - pad, hi + pad)
    drawn = [l for l in lows if len(l)]
    ax.set_ylim(min(l.min() for l in drawn) / 4 if drawn else 1e-2, 1.1)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=9, loc="lower left")
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
