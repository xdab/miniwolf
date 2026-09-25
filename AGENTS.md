# AGENTS.md

miniwolf is a minimalist C soundcard modem/TNC for amateur radio packet communications: AX.25 over 1200 baud AFSK (Bell 202), Linux-only, ALSA audio. See [README.md](README.md) for the user-facing story (features, install, usage, CLI options, configuration).

## Project layout

| Path                  | Contents                                                                                            |
| --------------------- | --------------------------------------------------------------------------------------------------- |
| `src/main.c`          | Entry point: options, audio, main loop, cleanup                                                     |
| `src/loop.c`          | Real-time loop and `audio_input_callback` — the RX/TX packet routing core                            |
| `src/miniwolf.c`      | Wiring of all interface layers (TCP/UDP/UDS servers, stdin/stdout, TNC2 extras/telemetry)            |
| `src/audio.c`         | ALSA initialization and callback glue (`aud_*`)                                                      |
| `src/options*.c`      | CLI parsing (argp) and config-file parsing; keys defined in `include/options.h`                      |
| `src/calibrate.c`     | Spectrum analyzer mode (`-C`), 8 bins with 1200 Hz reference                                         |
| `src/main_bench.c`    | `mw_bench`: offline demodulation from binary audio files                                             |
| `src/ring.c`          | Lock-free ring buffers (`ring_*`), compiled directly into the executables                            |
| `src/` (rest)         | DSP and modem libraries, see Architecture below                                                      |
| `include/`            | Public headers for everything in `src/`                                                              |
| `libs/libtnc/`        | Submodule: AX.25, HDLC, KISS, TNC2, CRC-CCITT, conf, line, buffer, logging (`common.h`)              |
| `libs/libcomm/`       | Submodule: TCP, UDP, UDS, socket poller, net helpers                                                 |
| `test/`               | Unit tests: `test_*.h` headers + `main_test.c` runner                                                |
| `systemd/`            | `miniwolf.service`, installed by `make install`                                                      |

## Build & test

Prerequisites: Linux with ALSA, GCC or Clang, CMake, ALSA dev headers (`libasound2-dev`). Submodules are required:

```bash
git submodule update --init --recursive
make release   # release build into build/
make build     # debug build (with -pg -O0 -DDEBUG)
make test      # build, then run build/mw_test
make run       # debug build, then run against sample.conf
make cal       # run the spectrum analyzer (-C)
make clean     # remove build/
```

CMake targets: static libs `dsp`, `mw_modem`, plus submodules `tnc` and `comm`; executables `miniwolf`, `mw_test`, `mw_bench`. Release builds are stripped and LTO-optimized.

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
- CI: GitHub Actions builds on push (`.github/workflows/c-cpp.yml`)
- Update [README.md](README.md) whenever CLI options or user-visible behavior change

## Constraints

- **Real-time:** no malloc/free, locks, IO, or syscalls inside `audio_input_callback` and anything it calls — static buffers and pre-allocated structures only
- **Linux/ALSA only** by design; do not add portability layers
- Do not turn miniwolf into a full APRS station (no digipeating, beaconing, APRS-IS) — pair with external tools instead, see README
- The `package` Makefile target is stale: it still tries to copy the removed `mw_cal` and `mw_log` binaries
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
