# AGENTS.md

miniwolf is a minimalist C soundcard modem/TNC for amateur radio packet communications: AX.25 over 1200 baud AFSK (Bell 202), Linux-only, ALSA audio. See [README.md](README.md) for the user-facing story (features, install, usage, CLI options, configuration).

## Orientation

The user-facing story (features, install, CLI options, configuration) lives in [README.md](README.md); the target inventory is `make help`. Layout is what `ls` shows; the load-bearing pieces:

- `src/main.c` — entry point; `src/loop.c` — real-time loop and `audio_input_callback`, the RX/TX packet routing core; `src/miniwolf.c` — wiring of all interface layers
- `src/` (rest) — DSP and modem libraries, see Architecture below; public headers in `include/`
- `libs/libtnc/` and `libs/libcomm/` — required submodules (protocols, sockets)
- `test/` — unit tests: `test_*.h` headers + `main_test.c` runner
- `systemd/` — service units and the TCP audio bridge script, installed by `make install` (details in [systemd/README.md](systemd/README.md))

## Build & test

Prerequisites: Linux with ALSA, GCC or Clang, CMake, ALSA dev headers (`libasound2-dev`). Submodules are required:

```bash
git submodule update --init --recursive
```

See `make help` for targets. CMake targets: static libs `dsp`, `mw_modem`, plus submodules `tnc` and `comm`; executables `miniwolf`, `mw_test`, `mw_bench`. Release builds are stripped and LTO-optimized.

## Architecture

Two data paths meet in `src/loop.c`:

- **RX (real-time):** ALSA capture → `audio_input_callback` → `modem_demodulate` (wraps `md_multi_rx`) → frames → KISS/TNC2 encode → stdin/stdout, TCP, UDP, UDS outputs
- **TX:** stdin/TCP/UDP/UDS input (KISS decoder or TNC2 line reader) → `modem_modulate` (`md_tx`) → ALSA playback

Libraries, in dependency order:

- `dsp`: `bf_*` Butterworth filters, `bf_biquad_*` 2200 Hz EQ, `agc_*` gain control, `fft_*` radix-2 FFT, `grz_*` Goertzel, `mavg_*`/`ema_*` averages, `synth`
- `mw_modem`: `md_rx_*`/`md_tx_*`/`md_multi_rx_*`/`modem_*` chains, `demod_*` demodulators, `bitclk_*` PLL bit-clock recovery, `mod_*` FSK modulator, `sql_*` squelch
- `libtnc` (submodule): protocols — `ax25_*`, `hldc_*`, `kiss_*`, `tnc2_*`, `crc_*`, plus `conf`, `line`, `buffer`, and logging macros in `common.h`
- `libcomm` (submodule): `tcp_*`, `udp_*`, `uds_*`, `poller_*` socket multiplexing

Demodulation: up to `MD_RX_MAX` (6) parallel demodulators per RX chain, but only three demodulator types currently exist in `include/demod.h`: `DEMOD_GOERTZEL_OPTIM`, `DEMOD_GOERTZEL_PESIM`, `DEMOD_QUADRATURE` (`DEMOD_ALL` is the union). Each produces soft bits → `bitclk_*` PLL → hard symbols → HDLC deframe. Frames seen from multiple demodulators are deduplicated by CRC with a 1 s window in `md_multi_rx` (see `src/modem.c`).

## Tech stack

- C11, CMake ≥ 3.10, GNU Make
- ALSA (`libasound2-dev`), argp for CLI parsing
- 32-bit `float` throughout DSP — never `double`

## Conventions

- **Function prefixes** group modules: `bf_` (filters), `aud_` (audio), `agc_`, `fft_`, `grz_`, `demod_*`, `bitclk_*`, `md_rx_*`/`md_tx_*`/`md_multi_rx_*`/`modem_*`, `mod_*`, `sql_` (squelch), `ring_*`, `mavg_*`/`ema_*`, plus `ax25_`/`kiss_`/`tnc2_`/`hldc_` (libtnc) and `tcp_`/`udp_`/`uds_`/`poller_` (libcomm)
- **Struct-based modules** with `init()` / `process()` / `free()` lifecycle
- **Minimal comments** — self-explanatory code preferred
- **Buffer safety:** always pass the buffer size and verify capacity before writes
- **Error handling:** `0` = success, negative = error; `goto` for cleanup only in top-level functions
- **Logging** (macros in `libs/libtnc/include/common.h`): `LOG` (always), `LOGV` (`-v`), `LOGD` (`-V`), `EXIT`/`EXITIF` (fatal), `nonnull`/`nonzero` (input validation). Messages: lowercase start, no trailing period or newline (auto-added)

## Quality gates

- `make test` must pass — unit tests live in `test/test_*.h` as `void test_name(void)` functions registered in `test/main_test.c`, using assertion macros from `test/test.h` (`assert_equal_int`, `assert_equal_float`, `assert_true`, `assert_memory`, `assert_string`)
- Test coverage expectations: normal operation, boundary conditions (empty/full/wrap), init/free pairs, diverse and noisy inputs (see `test/test_modem.h` for modem round-trip patterns)
- `make fmt-check` must pass — clang-format gate, config in `.clang-format`; reformat with `make fmt`
- `make tidy` must pass (needs a configured `build/`) — clang-tidy gate, check set in `.clang-tidy`; findings are triaged one by one: fix the code, or a `// NOLINT(check)` at the site; no check-wide disables without an attached reason
- CI: GitHub Actions runs build, test, fmt-check and tidy on push (`.github/workflows/c-cpp.yml`)

### Documentation

- no historical narratives (war stories) — docs describe the **current** state of things — let history be handled by Git alone; the same applies to source comments
- no pointless duplication — if some fact can be read off a `--help`, `ls` or `make help`, leave it out
- every in-repo reference must resolve correctly: file exists, cited symbol is in it, description is accurate
- material is scoped to the appropriate location, e.g. subdirectory details live in their READMEs

## Constraints

- **Real-time:** no malloc/free, locks, IO, or syscalls inside `audio_input_callback` and anything it calls — static buffers and pre-allocated structures only
- **Linux/ALSA only** by design; do not add portability layers
- Do not turn miniwolf into a full APRS station (no digipeating, beaconing, APRS-IS) — pair with external tools instead, see README
- Submodules `libs/libtnc` and `libs/libcomm` are required for the build; protocol changes belong there, not in `src/`

## Glossary

- **TNC2** — line-based text packet format (`SRC>DST,PATH:payload`), one packet per line
- **KISS** — binary framing protocol between host and TNC
- **AFSK / Bell 202** — 1200 baud audio modulation; mark = 2200 Hz, space = 1200 Hz
- **HDLC** — bit-oriented framing (flags, bit stuffing) carrying AX.25
- **AGC** — automatic gain control
- **APRS** — amateur radio position/messaging protocol built on AX.25

## Guiding principles

One focused tool that does one job well — DSP and modem in-process, everything else delegated to external tools over stdin/stdout, TCP, UDP, or UDS. Keep it small: static allocation, no dependencies beyond ALSA and the two submodules.
