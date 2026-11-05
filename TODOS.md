# TODOS

> Only outstanding or planned work. Done work is deleted, never ticked; git history remembers it if needed.
> No particular order, priorities, statuses, assignees, changelogs. Entries are simply statements.
> Written for a stranger. Every entry must make sense cold without conversation context, including to an outside contributor or AI agent.

## Device abstraction: miniwolf / miniwolf_file / miniwolf_rtl

Origin: rxx_lora's pull-only device layer (`include/device.h`,
`device_file.c`/`device_rtl.c`/`device_rsp.c` behind one interface, one
`receiver.c` core). miniwolf today has two parallel, drifting ingest paths:
the live ALSA chain in `src/loop.c` (hbf filter → squelch → AGC → modem) and
a hand-rolled separate chain in `src/main_bench.c` (own convert, eq,
squelch, direct `md_multi_rx_process_at`, no AGC). Intended shape: one
pull-only float-sample source interface with `device_alsa.c` (wrapping what
`src/audio.c` does) and `device_file.c` (raw PCM/WAV, absorbing bench's
`convert_sample`); `miniwolf` and `miniwolf_file` become thin mains over the
same RX chain (bench keeps only its timing wrapper). Also planned behind the
same interface: `miniwolf_rtl`, reading I/Q **directly from an RTL-SDR**
(rtl_tcp or librtlsdr sync reads) to own the DSP from the earliest stage —
channelize/AFSK-demodulate straight from RF samples instead of a soundcard
in the middle; the capture `-R` path from the harness entry falls out as a
recording sink over the same interface. Acceptance test: `tools/frame.py`
output decodes through the unified chain.

## tools/regression.py commit gate (synthetic-first) and its perf twin

Distinct from the capture harness below (that one needs real recordings and
ground truth; this one can exist today). Origin: rxx_lora's
`tools/regression.py` — the script that runs on every landing and fails the
commit — and `tools/regression_perf.py`, its speed twin (same verdicts plus
real-time multiplier per line; verdicts gate the exit code, never the speed).
Intended shape for miniwolf: build the case matrix out of what already works —
`tools/frame.py` variants (clean, each impairment arm once it exists, digi
paths, min/max frame sizes, `--phase-reset`) decoded through the bench entry
point (`mw_bench -f ... -F S16 ...`) with `--digi`/text ground truth compared
against the TNC2 stdout, plus the WA8LMF tracks in `data/` with their measured
baseline counts (track01: 1010, track02: 934; 5–7 decode zero today — that is
a known baseline, not a failure). PASS/FAIL matrix on stdout, non-zero exit on
any regression. Perf twin wraps the same cases in `mw_bench`'s existing
timing output and records decode-time-per-track so the zero-copy and
vectorization entries can prove their numbers. Blocked on nothing.

## Capture-and-regression measurement harness

The demod parameter commits ("optimized for 907 packets on track 1", "for
980 packets", "for 1007 packets") were each tuned by hand against one tape
recording, and today there is no way to answer "did this change hurt
decoding?" except running it by ear. The intended shape mirrors what
rxx_lora does: recordings of real packet audio cut into per-frame cases with
ground-truth AX.25 attached, and a gate script (`tools/regression.py` in
spirit, whatever it is here) that decodes every case through the bench
entry point and compares — green before any landing. The pieces: a raw
capture path (`miniwolf -R`-style recording of the ALSA input), a frame
cutter tolerant of collisions and noise, and the gate itself with ground
truth captured from a known-good TNC alongside. This harness is the
critical enabler for nearly everything else in this file — demod scoring,
PLL work, and performance claims are all guesses until it exists. Open
question: whether ground truth comes from Direwolf on the same audio or
from hand-verified decodes of clean captures.

## Paired-seed BER sweep tooling

Origin: rxx_lora's `tools/ber.py` / `ber_combine.py`, which miniwolf has no
analogue of. The intended shape: generate synthetic Bell202 frames, degrade
them at calibrated SNR (AWGN arm at minimum; volume and frequency-offset
arms are the natural extensions), sweep the noise ladder, and plot
PER-vs-SNR for two receiver variants on **common seeds** — paired A/B, so a
demod change on identical randomness means differences are attributable to
the change. Critical: without it, "the correlator demod beats Goertzel at
low SNR" is unfalsifiable. The existing `main_bench` and `test/` unit tests
exercise correctness, not sensitivity; this is the tool that measures the
latter. Not the intended shape: per-variant independently chosen seeds —
that reintroduces luck as a variable.

## Re-score the surviving demods and cull by data

Origin: three demodulators (quadrature, Goertzel, split-filter) are kept in
`src/demod_quad.c`, `src/demod_goertzel.c`, and `src/demod.c`, each
hand-tuned on one tape; two others ("subpar") were already removed by
vibes. Problem: nothing today says whether all three still earn their
place, or which wins at what SNR. Intended shape: once the regression
harness and BER sweep exist, score all three on the same captures and
sweeps; keep the winners with measured justification, delete the rest.
This entry deliberately waits on the two harness entries above —
running the comparison now would just repeat the hand-tuned-tape ritual.

## Dual SNR accounting: audio SNR and FM-equivalent RF C/N

Extension to the BER sweep above. rxx_lora sweeps a single well-defined
SNR; an AFSK-over-FM receiver has two meaningful noise references and
conflating them makes published numbers incomparable. Axis 1 — **audio
SNR**: noise power measured directly on the demodulated Bell202 audio miniwolf
consumes (the natural number for demod A/B, and what the AWGN arm applies).
Axis 2 — **FM-equivalent RF C/N**: the carrier-to-noise ratio an amateur
transceiver's FM demodulator needs *at RF* to produce that audio SNR at its
speaker output — computed through the FM improvement factor and the FM
threshold/capture effect (~12 dB C/N knee, below which audio SNR collapses
regardless of demod audio SNR). This is the number comparable against
real-world statements like "the packet was decodable at 12 dB SINAD" or
handheld specs. Intended shape: the BER table prints both columns — the
measured audio SNR actually applied, and the derived RF C/N via the standard
FM relation — so a PER-vs-SNR plot can be annotated in whichever reference
the reader thinks in. Blocked on the BER sweep existing; the math is a
closed-form overlay like rxx_lora's `--theory` BER line.

## Matched-filter correlator demodulator

The current demods estimate instantaneous frequency (quad) or single-bin
energy (Goertzel) per symbol; neither correlates against the 1200/2200 Hz
symbol templates, which is the standard way to buy low-SNR margin in AFSK
— the analogue of rxx_lora's dechirp-then-FFT, where the deterministic
shape of the symbol is exploited instead of just its energy. Intended
shape: a `demod_corr.c`-style variant producing soft symbol confidence
alongside the hard bit, slotted behind the existing demod interface so the
regression gate can score it against the incumbents on equal footing.
Criticality: a wish, not a gap — worth doing only because the harness
makes its verdict cheap.

## Soft-bit path out of the demod

Problem: demodulators emit hard bits and the HDLC layer in libtnc eats
them; per-bit confidence (eye amplitude, transition density, or
`bitclk.signal_quality`) is computed and discarded. Intended shape: an
optional confidence output per bit, consumed first by a retry-on-CRC-fail
path and later, if it pays, by Viterbi-style sequence scoring over
candidate bit offsets. Criticality: uncertain until the harness measures
how many CRC-fail frames a ±1 bit-slip retry would have recovered — that
number decides whether this is a real feature or a drawer item.

## DC offset and frequency-error tolerance

Problem: the split-filter parameters bake in nominal 1200/2200 Hz centers,
so an off-tune receiver or a zero-IF soundcard with DC spike degrades
silently; rxx_lora hit the same wall and solved it with a one-pole IIR DC
blocker plus per-frame CFO adaptation. Intended shape: a cheap DC blocker
on the capture path (a few lines, no opinion on cost) and, if captures
prove frequency error matters, an estimator that recenters the demod
filters per signal. Open question: how common off-tune reception actually
is — measure from real captures before building the estimator; the DC
blocker part is probably justified already.

## PLL characterization before extension

The bit-clock PLL (`src/bitclk.c`) has inertia ramps, a 32-bit transition
history, and DCD hysteresis (lock at 26, unlock at 12 good transitions) —
but nothing measured: lock acquisition time vs. level and noise, behavior
inside the hysteresis band, recovery after squelch bursts, and how much of
the AFSK packet-ear start it misses. Intended shape: characterization from
the capture harness (per-case verdicts should record whether DCD ever
locked), and only then decisions about look-ahead/rewind recovery for
frames lost to late lock — the receiver already buffers, so rewinding to
try an earlier start is available and unused. Not the intended shape:
tuning the inertia constants further against one tape; that is the ritual
this file exists to end.

## Zero-copy audio ingress

Problem: capture blocks hop through per-block `float_buffer_t` allocations
(`libs/libtnc/include/buffer.h`) on the hot path, and the playback ring is
the only pre-sized buffer. rxx_lora's equivalent change (zero-copy
ingress, pre-sized rings, no hot-path reallocs) took ~39% off wall clock
without touching decoded output. Intended shape: caller-owned scratch
through the capture path, rings sized at init so steady state never
allocates. Criticality: real but modest — a 1200-baud modem at 48 kHz is
nowhere near the CPU ceiling of a LoRa wideband receiver, so measure with
callgrind before and after rather than assuming the same magnitude.

## Vectorized demod inner loops

The demod/filter dot products are scalar float; rxx_lora's form
(`vector_size(32)` whole-vector dots with pairwise horizontal sums,
explicitly shaped because GCC will not vectorize a scalar FP reduction)
halved instruction counts in its hot loops. Intended shape: same form
applied only where a profile says the time actually goes, and kept only if
bit-exact or PER-neutral against the regression gate. Criticality: low
until profiling says otherwise — listed so it is not rediscovered
independently later.

## Port rxx_lora's method.md and bootstrap an AGENTS.md

rxx_lora carries `docs/method.md` — the receiver-methods document (DSP
chain rationale, why each stage exists, what was tried and rejected) — and
an `AGENTS.md` that orients AI agents/contributors: repo architecture in
one screen, where constraints and gates live, pointer discipline
("anything mechanical is discoverable; don't duplicate it here"). miniwolf
has neither: the demod/filter/bitclk design rationale exists nowhere but
git history and one tape-recording commit message each. Intended shape: a
`docs/method.md` (or equivalent) adapted to miniwolf's chain — split-filter
vs quad vs Goertzel demod history, bitclk PLL structure, squelch/AGC
roles, the WA8LMF tuning history and its baseline counts — and a root
`AGENTS.md` pointing at TODOS.md, `data/README.md` and the method doc,
mirroring rxx_lora's rule that subdirectory READMEs stay current. Keep the
rxx_lora docstring discipline while porting: no history war stories in
code comments, measured numbers live in the docs that exist to hold them.
Pairs naturally with the audio doc sweep below.

## Audio doc trim and factual sweep

`audio.md` narrates the capture/playback path in tutorial sections that
will drift; the README carries its own LLM-era warning ("Parts of the code
and docs may be LLM-written"). Intended shape: `audio.md` cut to
current-state reference (lifecycle, buffer ownership, error recovery —
what a maintainer needs), and a pass over all docs such that every claim
matches the code today and every referenced file and symbol resolves.
Criticality: cheap, and it keeps the doc honest for the next reader.
