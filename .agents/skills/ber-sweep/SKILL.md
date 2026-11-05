---
name: ber-sweep
description: Produce PER-vs-C/N sweeps and plots for the miniwolf RX chain with tools/ber.py, overlay saved sweeps with ber_combine.py, or A/B a receiver knob or two binaries on paired seeds. Use whenever the user mentions a BER/PER curve, sweep, waterfall, C/N performance, or wants to measure/compare receiver variants — even if they just say "run a sweep" or "plot it".
---

# PER sweep

Monte Carlo end-to-end packet PER vs C/N (at the IF output) for
`build/miniwolf_file`. The division of labor: `tools/ber.py` runs a sweep and
prints the table to stdout, saved stdout logs are the currency of everything
else, `tools/ber_combine.py` overlays logs on one axes. Every flag is in each
tool's `--help`; this skill is the workflow around them.

## House defaults

Unless asked otherwise, sweep the static reference channel (no `--mobile`,
no `--cfo`) at 48 kHz, 30-char payloads:

```sh
--cnr-min 3 --cnr-max 9 --cnr-step 0.5
```

The window brackets the measured static knee (PER 50% at 4 dB C/N, clean
from 7 dB — measured numbers live in `data/README.md`). 100 trials per point
while probing, 300+ for a curve someone will read numbers off; go higher when
the error counts say the waterfall is under-sampled. Sweeps parallelize over
trials with `--jobs`, which defaults to every core — pass
`--jobs $(( $(nproc) - 2 ))` to leave the machine breathing room; the draws,
summary and verdicts are identical either way.

There is no closed-form theory overlay in this repo's tooling. The ways a
sweep can say "no" are the selftest, the `wrong` column (false decodes must
stay zero on any credible run), and the WA8LMF tracks as the real-audio
reference.

Build first — `ber.py` drives `build/miniwolf_file` and does not build it.
Keep outputs in one tmp directory per session so logs stay reusable:

```sh
make release
mkdir -p /tmp/ber
python3 tools/ber.py --cnr-min 3 --cnr-max 9 --cnr-step 0.5 \
    --trials 300 --out /tmp/ber/static.png | tee /tmp/ber/static.log
```

Always `tee` the stdout table to a `.log`. Replotting a saved log with
`ber_combine.py` is free; re-sweeping costs wall time, and every later step
(A/B diff, overlay) consumes logs, not PNGs. If the harness itself is suspect
(`miniwolf_file` or `ber.py` just changed), run `python3 tools/ber.py
--selftest` first: one fixed trial, exit 1 on mismatch.

## Probe first, then zoom

Don't open with a huge sweep. A probe — fewer trials, wider window, coarser
step — locates the waterfall cheaply; the zoom then spends the full trials
only where the curve bends:

```sh
# probe: just enough to see where errors start
python3 tools/ber.py --trials 25 --cnr-min 0 --cnr-max 20 --cnr-step 2 \
    --out /tmp/ber/probe.png | tee /tmp/ber/probe.log
# zoom: full trials on the region the probe found
python3 tools/ber.py --trials 300 --cnr-min 3 --cnr-max 9 --cnr-step 0.5 \
    --out /tmp/ber/static.png | tee /tmp/ber/static.log
```

The probe also answers "how long will the big one take": time it, then scale
linearly in `trials × points`. Quote the estimate before launching a
genuinely large sweep rather than discovering it an hour in.

## Sizing the run

A 0-error point is an upper bound, not a measurement. Read the error
**counts** in the table, not just the PER column: a flat zero low end means
the window starts too high or the trials are too few to see first flips.
Extend the window or raise trials until the waterfall is observed on both
ends.

## A/B: paired draws

The same `--seed` draws the same trials in both runs, so two sweeps differing
only in `--bench-args` or `--bin` align line for line under `--verdicts` —
the attribution is a plain `diff`:

```sh
python3 tools/ber.py ... --verdicts /tmp/ber/base.v | tee /tmp/ber/base.log
python3 tools/ber.py ... --bench-args "-s 0.5" \
    --verdicts /tmp/ber/knob.v | tee /tmp/ber/knob.log
diff /tmp/ber/base.v /tmp/ber/knob.v
python3 tools/ber_combine.py --out /tmp/ber/ab.png \
    /tmp/ber/base.log /tmp/ber/knob.log
```

Judge the A/B on the diff before the plot — the diff names exactly which
trials flipped verdicts, while a curve can move within noise and still look
convincing. Compare configurations with identical seed, trials, C/N grid,
text length and channel arms; anything else is a confound, not a result.
This is why probe and zoom logs land in `/tmp/ber` even unasked: the base arm
of a future comparison already exists.

## Loss attribution

A PER number says how many trials died; `tools/loss_decomp.py` says where.
`ber.py` always passes `--stats` to the binary and appends the per-demod
counters (bits, DCD-locked samples, completed frames) and chain counters
(squelch drops) to every `--verdicts` line; the decomposition stacks them per
C/N into `squelch` / `dcd` / `deframe` / `dedup` / `wrong` / `ok` bands plus
a per-demodulator frames table (which demod completes frames where — the
data behind the demod-scoring question in TODOS.md):

```sh
python3 tools/loss_decomp.py /tmp/ber/base.v --out /tmp/ber/loss.png
python3 tools/loss_decomp.py /tmp/ber/base.v /tmp/ber/knob.v \
    --labels baseline knob --out /tmp/ber/ab_loss.png
```

Judge bands, not just the curve: a `deframe`-band loss has the opposite cure
of a `squelch` one (method.md: "wrong choice" vs "right answer never
offered"). Verdict files from an older binary decompose into `unknown`.
