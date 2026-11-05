# AGENTS.md

`README.md` is the human front page; this is AI agents'.
Please read the main README text before substantial works to gain basic understanding of the project.

This document carries what you cannot get from elsewhere: constraints, gates, purposes, orientation.
Anything mechanical — build targets (`make help`), CLI flags (`./build/miniwolf -h`, `./build/miniwolf_file -h`),
the source map, the `tools/` inventory — is easily discoverable, so don't put it here.

## Repo architecture

Sources live in `src/`, headers in `include/`. Shared packet-layer code
(HDLC, AX.25, KISS, CRC, TNC2, buffers) lives in `libs/libtnc/`; `libs/libcomm/`
holds the socket/IPC layers. Three binaries build from the same core:

- `build/miniwolf` — the live soundcard TNC (`src/main.c`, `src/loop.c`): ALSA
  capture (via `src/device_alsa.c`) → shared RX chain (`src/rx.c`: high-boost
  filter `src/filter.c` → AGC `src/agc.c` → squelch `src/squelch.c` →
  `src/modem.c`, driving the demodulators) → libtnc → TNC2/KISS over
  stdin/stdout/TCP/UDP/UDS. `src/calibrate.c` is merged into this executable.
- `build/miniwolf_file` — the offline demodulator (`src/main_file.c`): raw or
  WAV audio in (`src/device_file.c`), same RX chain (`src/rx.c`), decodes to
  stderr. This is the entry point every tool and future gate scores through;
  the source of both front ends is one pull-only device interface
  (`include/device.h`).
- `build/mw_test` — unit tests (`test/`), run via `make test`.

The modem runs three demodulators behind one interface (`src/demod.c`,
`src/demod_goertzel.c`, `src/demod_quad.c`); the bit clock is a PLL
(`src/bitclk.c`) with DCD hysteresis. Their relative merit is an open,
measurement-defined question (TODOS.md) — do not hand-tune their constants
against a single recording; that is the ritual this repo's tooling exists to
end.

`tools/` is the Python toolkit: `frame.py` (Bell202 generator + ground
truth), `channel.py` (the transceiver channel: mobile fading, tone offset,
CNR noise at the IF output), `ber.py` (PER-vs-C/N sweeps, paired seeds),
`ber_combine.py` (overlay sweep logs), `loss_decomp.py` (stage + per-demod
loss attribution over `ber.py --verdicts` files). All stochastic tools take a `--seed`;
two runs sharing it draw identical channels (paired A/B).

`data/` holds real capture material (WA8LMF TNC Test CD derivatives),
gitignored; `data/README.md` lists the files and their measured decode-count
baselines.

## Quality gates

- clean `make build` / `make release`;
- `make test` passing (`build/mw_test`);
- `python3 tools/ber.py --selftest` passing;
- `./build/miniwolf_file -f <file> -r <rate>` on a `tools/frame.py` output
  decoding exactly the ground-truth TNC2 line.

There is no commit gate script yet (TODOS.md); the checks above are the
manual equivalent until it lands.

## Documentation

- docs describe the **current** state of things — no historical narratives,
  no war stories; git history remembers the past;
- no pointless duplication — if a fact is readable off a `--help`, an `ls`,
  or the code, leave it out;
- every in-repo reference must resolve: the file exists, the search string is
  in it, the description is accurate;
- measured numbers live in the docs that exist to hold them
  (`data/README.md` for decode counts), not in code comments.

## Constraints

- C logging goes through the `LOG`/`LOGV`/`LOGD` macros
  (`libs/libtnc/include/common.h`) to stderr — packet decode lines included,
  which is why the tools parse stderr, never stdout;
- audio is mono, native-endian float32 between stages; file formats are
  converted at the edges only (`miniwolf_file`'s `-F`);
- Python tools are stdlib + numpy (+ matplotlib lazily in the plot path);
  keep it that way.

## Glossary, nomenclature

### Bell202 / AFSK

1200 baud audio frequency-shift keying: mark = 1200 Hz (tone bit 1), space =
2200 Hz (tone bit 0). The amateur packet standard on VHF.

### NRZI, HDLC, AX.25

NRZI encodes a data 0 as a tone transition, a 1 as no transition. HDLC wraps
the frame in 0x7E flags, bit-stuffs after five 1s, and protects it with a
CCITT (Kermit-reflected) FCS. AX.25 is the amateur HDLC profile: 7-byte
address fields, control 0x03, PID 0xF0 for UI/APRS frames.

### TNC2 / KISS

The two interfacing formats: TNC2 is the human-readable monitor line
(`SOURCE>DEST[,DIGI...]:payload`), KISS the binary framing protocol for
host-terminal communication. Both are available on every IPC layer.

### C/N (carrier-to-noise ratio)

The noise reference in this repo, in dB, measured at the **IF filter's
output** — where a transceiver's limiter sits. Noise is thermal, added at
RF, band-limited by the IF filter, and characterized before the
discriminator; audio SNR is not used anywhere in the tooling. Inside fades
the audio SNR collapses regardless of the C/N figure; C/N is the number
radio specs and ears agree on.

### Picket fencing

Rayleigh multipath fading as heard from a moving station: the received
signal surfs in and out of spatial nulls (tens of ms at VHF mobile speeds),
with the limiter spraying crackle through every null. In this repo's channel
model the distant station is mobile; the receiver and its noise floor are
stationary.

### DCD (Data Carrier Detect)

The modem's lock verdict, driven by transition density with hysteresis
(`src/bitclk.c`): it gates HDLC bit recovery and must unlock and re-lock
cleanly through squelch bursts and fades.

### PTT (push-to-talk)

The transmitter keying state. In the channel model the TX is on the air only
where audio is present — an unkeyed transmitter radiates nothing, so
inter-frame gaps carry receiver noise only.
