---
name: profiling
description: Profile the miniwolf RX chain with valgrind + callgrind — function/line/call-site Ir attribution and the wall-clock twin, via make profile and tools/prof/hotspots.py. Use when the user asks where time or instructions go, why something is slow, wants a profile or hotspots, names callgrind/valgrind/Ir, or claims an optimization — even if they just say "profile it" or "what's eating the cycles".
---

# Profiling

Every performance verdict rests on **two metrics, both measured**:

- **Ir** — instruction counts from callgrind. Deterministic for a given
  binary + input, so two runs compare exactly and attribution goes down to a
  line. This is the _finder_.
- **Wall clock** — seconds. `miniwolf_file` prints its modem-only time
  (`Time in modem_demodulate`); plain `time` on the release binary gives the
  end-to-end figure. This is the _confirmer_: the user feels seconds, not
  instructions, and an Ir win can vanish in cache or branch effects.

A change is only real when both move. Quote both numbers in any performance
claim.

## The binary

```sh
make profile
```

builds `build/prof/miniwolf_file` with the production codegen
(`-O3 -DNDEBUG -flto`) plus `-g -fno-omit-frame-pointer` — callgrind gets
symbols while the instructions stay the release instructions. Never profile
(or time) the `make build` tree: it is `-pg -O0 -DDEBUG`, and its hot spots
are the optimizer's absence, not the code's. A stale `build/prof` relinks
without current sources — `make profile` re-runs cmake, but after touching
CMakeLists itself, delete `build/prof` first; every function showing as `???`
means the profile objects are stale.

## Quick start

Any short `.f32`/WAV input works — a `tools/frame.py` output (~0.7 s of
audio) or a cut of a WA8LMF track. Callgrind runs ~50× slower than native,
so keep inputs short; the Ir counts are exact regardless of length.

```sh
python3 tools/frame.py --text "hello world" -o /tmp/frame.f32
valgrind --tool=callgrind --callgrind-out-file=/tmp/cg.out --cache-sim=yes \
  build/prof/miniwolf_file -f /tmp/frame.f32 -r 48000
python3 tools/prof/hotspots.py /tmp/cg.out
callgrind_annotate --tree=caller --threshold=95 /tmp/cg.out | less
```

## The two views

Both read the same `callgrind.out`; cross-check them when in doubt.

**`tools/prof/hotspots.py`** — the compact view and the default deliverable.
Its functions table is the answer to "where does it go":

```
function                            Ir       Ir %
src/filter.c:demod_process       7.778M   30.23%
```

Flags: `--top N` functions shown, `--lines N` hot source lines per file
(with the source text), `--calls N` call sites with inclusive cost and call
counts, `--sort EVENT` to re-rank by any recorded event (`D1mr` cache-read
misses, `D1mw`, `DLmr`), `--callers NAME` for who calls a function,
`--selftest` to verify the parser.

**`callgrind_annotate`** — the same data as a full annotated source listing.
`--auto=yes` (default) annotates every source file past the threshold —
line-by-line attribution. `--tree=caller|callee` prints who calls / what is
called per function, the fast way to separate per-run cost from one-time
init. `--threshold=95` trims the tail; `--context=N` widens the source
window.

## Reading a profile

1. **Totals.** hotspots.py's first line — sanity-check the Ir total against
   the input size before trusting anything below it.
2. **Functions (self).** The top of the self table is where the cycles go.
   Group entries by the code they belong to; `[bracketed]` names tag
   symbol-only frames with their `.so`.
3. **Call sites (inclusive).** For each hot function, find who pays. A
   function called once from init amortises away on a long stream; one
   called per sample from the hot loop is the real target.
4. **Re-sort by cache misses.** `--sort D1mr --lines 0 --calls 0` — a
   compute-bound function with high `D1mr` is where a data-layout change
   pays. Repeat with `D1mw`, then `DLmr`.
5. **Hot lines.** `--lines N` prints the hottest statements per file with
   their source text — usually the one line to change.
6. **Attribute.** `--callers NAME` or `--tree=caller` to see whether the
   caller is miniwolf code, libm, or a library kernel.

Conventions that keep the reading honest:

- **Self vs inclusive.** Self = cost inside the function; inclusive adds
  everything it calls. Sum the self column, never the inclusive one.
- **Recursion double-counts inclusive cost** — trust the call-site table for
  recursive functions.
- **`???` with no name** means the profile objects went stale: rebuild.
  A named `???` (libm, librt) is just a library without debug info and is
  attributed fine.
- **Ir is not seconds.** Cache-sim events are counts too. Wall clock has its
  own section above.

## Campaigns

An optimization campaign is a chain of paired A/Bs: one logical change per
commit, and the artifacts branded with the commit they belong to —

```sh
d=/tmp/prof/$(git rev-parse --short HEAD); mkdir -p "$d"
cp build/prof/miniwolf_file "$d"/ && valgrind --tool=callgrind \
  --callgrind-out-file="$d/cg.out" "$d/miniwolf_file" -f /tmp/frame.f32 -r 48000
```

An old profile stays exactly valid for the binary that produced it, so
comparing two commits is two `hotspots.py` runs on two archived outs — the
hash names every arm. Performance ideas are judged by exactly this protocol: baseline profile first, one change,
paired A/B, both metrics or it didn't happen.

## Gotchas

- One-time init (filter/biquad design, ALSA setup) over-weights short
  inputs; compare inputs of similar length or ignore init call sites when
  extrapolating to streaming.
- `--cache-sim=yes` adds the miss events; without it only Ir/Dr/Dw exist.
- Attribution moves with the binary: a current claim needs a current
  profile, which is why archived outs are branded per commit — never re-read
  an old profile as if it described today's binary.
