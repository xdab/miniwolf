# TODOS

> Only outstanding or planned work. Done work is deleted, never ticked; git history remembers it if needed.
> No particular order, priorities, statuses, assignees, changelogs. Entries are simply statements.
> Written for a stranger. Every entry must make sense cold without conversation context, including to an outside contributor or AI agent.

## miniwolf_rtl: RTL-SDR device backend

Origin: rxx_lora's pull-only device layer (`include/device.h`) now exists in
miniwolf with ALSA (`src/device_alsa.c`) and raw/WAV file
(`src/device_file.c`) backends behind it. Planned next: `device_rtl.c`,
reading I/Q **directly from an RTL-SDR** (rtl_tcp or librtlsdr sync reads) to
own the DSP from the earliest stage — channelize/AFSK-demodulate straight
from RF samples instead of a soundcard in the middle. A capture `-R`-style
recording sink falls out as another device over the same interface when
needed. Acceptance test: `tools/frame.py` output decodes through the new
backend as it does through `build/miniwolf_file`.

## Channel noise calibration: fix sigma scaling and duty referencing, re-baseline

The channel model under-scales noise by a factor of 2 (3.01 dB): the sigma
formula divides by 2 as if it were a per-component std-dev, but it
multiplies a unit-total-power complex shape, so actual noise power is
half the claimed C/N. Compounded with duty referencing (noise scaled to
the whole-file mean carrier power — 0.53 at 65% duty — instead of the
keyed carrier: +2.73 dB in-frame), a sweep labeled "C/N 4 dB" actually
tests the receiver at ~9.8 dB in-frame (audit-measured). All absolute
C/N labels on measured baselines and theory overlays are shifted
accordingly; paired-seed A/B comparisons remain valid (deterministic
offset). Fix the scaling, reference noise to the keyed carrier, re-run
the reference sweeps, re-record data/README.md. Measured since: the
arms differ by 2.4 dB true in-frame C/N (10.2 vs 7.8 dB at claimed
4 dB, limiter input) yet decode identically, and cutting the leading
hiss from failing trials (bit-identical in-frame noise) rescues 0/15 —
the loss is in-frame, not acquisition, and PER is invariant to the
measured in-frame noise-power difference. So noise POWER is not the
controlling quantity the receiver experiences; next instruments: the
carrier-frame phase-error estimator (expert-prescribed: u = z*conj(c)/
|c|^2, home region, count |dpsi| > pi excursions and the sub-2-pi
|dpsi| > pi/2 tail) and the sigma fix, after which all synthetic C/N
labels get re-recorded.

## Narrow the IF before the discriminator (near-Carson channel filter)

The IF noise bandwidth is 25 kHz against a Carson bandwidth of ~7.4 kHz
(dF=1.5 kHz) to ~10.4 kHz (dF=3 kHz) for the AFSK-over-FM link. Theory:
raising rho_lim by 10*log10(25/10) = 4 dB and shrinking sigma_f (the
Rice click rate scales with it) — the cheapest large win on the click
waterfall. The knob already exists (`--mobile-if-bw`); measure the PER
effect on the reference sweep, and check the demod's own audio path
still sees enough tone (the AFSK audio occupies ~1.8 kHz, well inside).

## Tone-detector window and click blanking

The Goertzel tone detectors integrate over ~3.2 bit periods: matched
integration is ~1 bit, so the long window buys SNR but adds ISI exactly
where the click waterfall sits, and a ~1-bit window with good clock
recovery lets a single clipped click no longer flip a decision at all
(per the theory agent's threshold argument). Companion: a post-
detection click blanker (blank the audio for ~1/B after a discriminator
rail hit), estimated 0.5-2 dB at the cliff. Both are demod-interface
changes and must be scored paired-seed against the recorded baseline.

## Re-score the surviving demods and cull by data

Origin: three demodulators (quadrature, Goertzel, split-filter) are kept in
`src/demod_quad.c`, `src/demod_goertzel.c`, and `src/demod.c`, each
hand-tuned on one tape; two others ("subpar") were already removed by
vibes. Measured (static channel, `--demods` solo arms, 300 trials/point,
data/README.md): quadrature is the best solo demodulator at every C/N; the
two Goertzel tunings (optim/pesim) are near-identical to each other and
~0.5 dB behind; in the default race goertzel-optim takes the output by
first-wins order while pesim wins almost nothing despite completing as
many frames as optim. Remaining: cull pesim (worst solo, structurally
voiceless in the race) and any further culling behind the regression
suite (tools/regression.py); re-verify on the WA8LMF tracks and real captures
before deleting anything. Multi-decoder diversity — several parallel
decoders with different filters, accept any CRC-valid frame — already
exists as the default race and measurably beats every solo arm by
~0.5 dB.

## Race winner order and selection

The default race delivers frames first-wins in demod-bit order, so
goertzel-optim (bit 0) shadows the others regardless of merit
(data/README.md: 300/300 wins from 6 dB). Measured: quad-first
(`--demods quad,optim`) is +0.7% PER at the knee and identical elsewhere
— quadrature completes more frames including marginal ones that pre-empt
optim's clean decodes, and costs more CPU — so optim-first stands.
Remaining ideas, each a paired-seed A/B
against the recorded race baseline: (b)
select the winning frame per frame by demod confidence instead of
completion order (needs the soft-bit path); (c) deliver ALL demods'
frames and let dedup collapse them, so order stops mattering. The race
currently beats every solo arm by ~0.5 dB, so any change must be
PER-neutral-or-better, not just cleaner.

## Matched-filter correlator demodulator

The current demods estimate instantaneous frequency (quad) or single-bin
energy (Goertzel) per symbol; neither correlates against the 1200/2200 Hz
symbol templates, which is the standard way to buy low-SNR margin in AFSK
— the analogue of rxx_lora's dechirp-then-FFT, where the deterministic
shape of the symbol is exploited instead of just its energy. Intended
shape: a `demod_corr.c`-style variant producing soft symbol confidence
alongside the hard bit, slotted behind the existing demod interface so the
regression suite (tools/regression.py) can score it against the incumbents on equal footing.
The far end of this direction: IF/baseband-domain detection (matched
filter or MLSE over the FM waveforms) has no discriminator and therefore
no click threshold at all — theory estimates a 5-7 dB gain over the
discriminator receiver at the cliff (MLSE bound ~0.1 dB C/N at 50% PER,
realistic 1-3 dB), but it needs a known deviation, carrier/tone-phase
reference and CFO tracking. Only worth building if the discriminator
chain is exhausted.
Criticality: a wish, not a gap — worth doing only because the harness
makes its verdict cheap.

## Soft-bit path out of the demod

Problem: demodulators emit hard bits and the HDLC layer in libtnc eats
them; per-bit confidence (eye amplitude, transition density, or
`bitclk.signal_quality`) is computed and discarded. Intended shape: an
optional confidence output per bit, consumed first by a retry-on-CRC-fail
path and later, if it pays, by Viterbi-style sequence scoring over
candidate bit offsets. Measured basis for the retry half: every loss at
the static-channel knee is in the `deframe` band (bits flow, no CRC-valid
frame completes — tools/loss_decomp.py), so ±1 bit-slip retries have raw
material to recover; the number to measure is how many of those frames a
retry would actually turn valid. Criticality: uncertain until the harness measures
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
the AFSK packet-ear start it misses. First datum from the loss-attribution
counters: DCD never locks at 22050 Hz (0 locked samples even behind 128
preamble flags; it locks at 48000 Hz) and never locks at any C/N on the
static reference channel below ~7 dB — bits are emitted regardless, so
decoding is unaffected, but the flag is dead exactly where the WA8LMF
tracks live. Intended shape: characterization from
the regression suite (per-case verdicts should record whether DCD ever
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

## Per-frame signal-quality estimate: audio SNR, C/N or Eb/N0 reported with each decoded frame

Problem: miniwolf decodes frames but reports nothing about the channel they
arrived through — the receiver already computes per-sample signal power (AGC,
squelch envelopes) and per-bit confidence (`bitclk.signal_quality`), yet a
decoded frame carries no quality figure, so operators and tools cannot tell a
clean decode from one scraped out of the noise. Intended shape: estimate the
received channel quality over each frame's audio span (the demodulators
already see the frame boundaries) and report it with the decode line — audio
SNR, C/N (the repo's noise reference, dB at the IF output), or Eb/N0, one
honest estimator rather than all three. Criticality: a wish — worth doing
because it costs little (the quantities are already computed internally) and
makes every capture, gate line and field report self-describing; the estimator
must be validated against the tools' known-C/N channels (tools/ber.py draws at
labeled C/N) before its numbers are trusted.

## Vectorized demod inner loops

The demod/filter dot products are scalar float; rxx_lora's form
(`vector_size(32)` whole-vector dots with pairwise horizontal sums,
explicitly shaped because GCC will not vectorize a scalar FP reduction)
halved instruction counts in its hot loops. Intended shape: same form
applied only where a profile says the time actually goes, and kept only if
bit-exact or PER-neutral against the regression suite. Criticality: low
until profiling says otherwise — listed so it is not rediscovered
independently later.
