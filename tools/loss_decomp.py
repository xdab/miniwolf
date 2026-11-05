#!/usr/bin/env python3
"""Loss decomposition: at each C/N, where a run's trials died.

References (in-repo):
- tools/ber.py --verdicts: the per-trial line format read here (the STATS
  fields come from miniwolf_file --stats via ber.py)
- .agents/skills/ber-sweep/SKILL.md — the sweep workflow that produces the
  verdict files

Per trial the deepest stage reached wins:

  ok       decoded and matched ground truth
  wrong    decoded, but none of the decodes matched (false decodes)
  dedup    frames completed internally, all suppressed by deduplication
  deframe  bits flowed but no CRC-valid frame completed
  dcd      demodulator ran, the bit clock never emitted a bit
  squelch  squelch dropped every sample before any demodulator saw it
  unknown  verdict line without STATS fields (older binary)

Because the verdict files are drawn from the same seed, two of them
(before/after one change) decompose onto identical trials, so the panels are
directly comparable: a bar that grows in `ok` was bought from the bands below
it.
"""

import argparse
import collections
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROW = re.compile(r"(\w+)=(\S+)")

# bottom -> top: the deepest stage sits at the top of the stack
ORDER = ["squelch", "dcd", "deframe", "dedup", "wrong", "ok", "unknown"]
LABELS = {
    "squelch": "squelch dropped all samples",
    "dcd": "no bit clock (DCD never locked)",
    "deframe": "bits, no CRC-valid frame",
    "dedup": "frames deduplicated away",
    "wrong": "false decodes only",
    "ok": "decoded",
    "unknown": "no STATS fields (older binary)",
}
COLORS = {
    "squelch": "#6a1b9a",
    "dcd": "#1565c0",
    "deframe": "#ef6c00",
    "dedup": "#8d6e63",
    "wrong": "#c62828",
    "ok": "#2e7d32",
    "unknown": "#9e9e9e",
}

# rx index -> demodulator name (modem.h bit order of DEMOD_* flags)
DEMOD_NAMES = ["goertzel-optim", "goertzel-pesim", "quadrature"]


def bucket(trial: dict) -> str:
    """Deepest stage reached for one verdict line."""
    if trial["outcome"] == "ok":
        return "ok"
    if trial["outcome"] == "wrong":
        return "wrong"
    has_stats = "samples_in" in trial
    if not has_stats:
        return "unknown"
    i = 0
    samples = bits = frames = 0
    while f"rx{i}_samples" in trial:
        samples = max(samples, int(trial[f"rx{i}_samples"]))
        bits = max(bits, int(trial[f"rx{i}_bits"]))
        frames = max(frames, int(trial[f"rx{i}_frames"]))
        i += 1
    if frames > 0 and trial["decodes"] == "0":
        return "dedup"
    if bits > 0:
        return "deframe"
    if samples > 0:
        return "dcd"
    if int(trial["samples_in"]) > 0:
        return "squelch"
    return "unknown"


def load_verdicts(path):
    """One verdict file -> per-C/N bucket, per-demod frames and wins tables."""
    rows = collections.defaultdict(collections.Counter)
    per_demod = collections.defaultdict(collections.Counter)
    per_wins = collections.defaultdict(collections.Counter)
    with open(path) as fh:
        for line in fh:
            trial = dict(ROW.findall(line))
            if not trial:
                continue
            cnr = float(trial["cnr"])
            rows[cnr][bucket(trial)] += 1
            i = 0
            while f"rx{i}_frames" in trial:
                per_demod[cnr][i] += int(trial[f"rx{i}_frames"])
                per_wins[cnr][i] += int(trial.get(f"rx{i}_wins", 0))
                i += 1
    return rows, per_demod, per_wins


def print_table(path, rows):
    snrs = sorted(rows)
    print(f"\n{os.path.basename(path)}")
    head = f"  {'C/N':>6} {'trials':>7}"
    head += "".join(f" {k:>8}" for k in ORDER)
    head += f" {'PER':>7}"
    print(head)
    for cnr in snrs:
        r = rows[cnr]
        total = sum(r.values())
        per = 1 - r.get("ok", 0) / total if total else 0.0
        line = f"  {cnr:6g} {total:7d}"
        line += "".join(f" {r.get(k, 0):8d}" for k in ORDER)
        line += f" {per:7.3f}"
        print(line)


def print_demods(path, per_demod, per_wins):
    """Which demodulator completes frames at which C/N (decode owners)."""
    snrs = sorted(per_demod)
    if not snrs:
        return
    max_rx = max(max(d) for d in per_demod.values()) + 1
    print(f"\n{os.path.basename(path)} — frames/wins per demodulator")
    head = f"  {'C/N':>6}"
    head += "".join(
        f" {DEMOD_NAMES[i] if i < len(DEMOD_NAMES) else f'rx{i}':>16}" for i in range(max_rx))
    print(head)
    for cnr in snrs:
        line = f"  {cnr:6g}"
        line += "".join(
            f" {per_demod[cnr].get(i, 0):>7d}/{per_wins[cnr].get(i, 0):<8d}" for i in range(max_rx))
        print(line)


def plot(files, labels, out):
    fig, axes = plt.subplots(
        1,
        len(files),
        figsize=(7 * len(files), 6),
        constrained_layout=True,
        sharey=True,
        squeeze=False,
    )
    axes = axes[0]
    for ax, path, label in zip(axes, files, labels):
        rows, _, _ = load_verdicts(path)
        snrs = sorted(rows)
        x = np.array(snrs)
        bottom = np.zeros(len(snrs))
        for key in ORDER:
            vals = np.array([rows[s].get(key, 0) for s in snrs], float)
            ax.fill_between(
                x, bottom, bottom + vals, label=LABELS[key], color=COLORS[key], step="mid"
            )
            bottom += vals
        ax.set_title(label)
        ax.set_xlabel("C/N at the IF output [dB]")
        ax.set_xlim(x.min(), x.max())
    axes[0].set_ylabel("trials")
    axes[0].legend(fontsize=9, loc="upper left")
    fig.savefig(out, dpi=110)
    print(f"\nwrote {out}")


def main():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  python3 tools/ber.py --cnr-min 0 --cnr-max 10 --trials 300 "
            "--verdicts /tmp/ber/base.v\n"
            "  python3 tools/loss_decomp.py /tmp/ber/base.v --out /tmp/ber/loss.png\n"
            "  python3 tools/loss_decomp.py base.v knob.v --labels baseline squelch "
            "--out /tmp/ber/ab.png\n"
        ),
    )
    p.add_argument("verdicts", nargs="+", metavar="VERDICTS", help="ber.py --verdicts files")
    p.add_argument("--labels", nargs="+", default=None, help="panel titles (default: file names)")
    p.add_argument("--out", default="loss_decomp.png", help="output PNG path")
    args = p.parse_args()

    labels = args.labels or [os.path.basename(v) for v in args.verdicts]
    if len(labels) != len(args.verdicts):
        p.error(f"--labels {len(labels)} != {len(args.verdicts)} verdict files")
    loaded = [load_verdicts(path) for path in args.verdicts]
    for path, (rows, per_demod, per_wins) in zip(args.verdicts, loaded):
        print_table(path, rows)
        print_demods(path, per_demod, per_wins)
    plot(args.verdicts, labels, args.out)


if __name__ == "__main__":
    main()
