![Build](https://github.com/xdab/miniwolf/actions/workflows/c-cpp.yml/badge.svg)

# miniwolf

_Danger! Parts of the code and docs may be LLM-written._

### What it is

Soundcard modem/TNC for amateur radio packet communications designed as a simple, lightweight alternative to well-known and respected [Direwolf by WB2OSZ](https://github.com/wb2osz/direwolf). 

It supports encoding and decoding AX.25 packets over 1200 baud AFSK while interfacing with existing tools over stdin/stdout, TCP as well as UDP. On each of these "interfacing layers" both TNC2 and KISS can be used.

### What it isn't

This project is **not** a:
- Drop-in replacement for Direwolf
  - The configuration is entirely CLI-based and non-persistent
- Cross-platform program 
  - It is built around ALSA, the Advanced **Linux** Sound Architecture, the implication is fairly obvious
- Multi-mode modem (for now)
  - Only Bell202 / 1200 baud AFSK is supported
     - hardcoded actually...
- Full-featured APRS station
  - No built-in digipeating, beaconing, APRS-IS connectivity
  
And this is **the whole point!** To have a focused tool that does one job well. Let other tools figure out the rest.

## Build and installation

While cloning the project is entirely standard, the building differs slightly from other projects using CMake. A root-level Makefile is specified with some shortcuts for building and even installing.

#### Prerequisites
- Linux
- GCC or Clang
- CMake
- ALSA development libraries (`libasound2-dev` on Debian/Ubuntu)

First steps may go like this:

```bash
git clone https://github.com/xdab/miniwolf.git
cd miniwolf
make build # builds with debug flags
make release # builds properly 
make install # builds in release mode and installs to the system
```

## Sample usage

```bash
# List available audio devices
miniwolf -l

# Receive and transmit with TCP KISS server on port 8100
miniwolf -d "default" -io -r 44100 --tcp-kiss 8100
```

## Detailed usage

### Audio setup
| Short option | Long option         | Description                                   |
| ------------ | ------------------- | --------------------------------------------- |
| `-l`         | `--list`            | List audio devices and exit                   |
| `-d NAME`    | `--dev=NAME`        | Audio device name (e.g., "default", "hw:1,0") |
| `-D INDEX`   | `--dev-index=INDEX` | Audio device by index                         |
| `-r RATE`    | `--rate=RATE`       | Sample rate in Hz (typically 44100 or 48000)  |
| `-i`         | `--input`           | Enable audio input (receive)                  |
| `-o`         | `--output`          | Enable audio output (transmit)                |

### Protocol
| Short option | Long option | Description                                        |
| ------------ | ----------- | -------------------------------------------------- |
| `-k`         | `--kiss`    | Use KISS protocol instead of TNC2 for stdin/stdout |

### Network
| Long option                                     | Description                                 |
| ----------------------------------------------- | ------------------------------------------- |
| `--tcp-kiss PORT`                               | Start TCP server for KISS clients           |
| `--tcp-tnc2 PORT`                               | Start TCP server for TNC2 clients           |
| `--udp-kiss-addr ADDR` / `--udp-kiss-port PORT` | Send received packets via UDP (KISS)        |
| `--udp-tnc2-addr ADDR` / `--udp-tnc2-port PORT` | Send received packets via UDP (TNC2)        |
| `--udp-kiss-listen PORT`                        | Listen for KISS packets to transmit via UDP |
| `--udp-tnc2-listen PORT`                        | Listen for TNC2 packets to transmit via UDP |

### Signal processing
| Short option | Long option     | Description                                                           |
| ------------ | --------------- | --------------------------------------------------------------------- |
| `-s`         | `--squelch`     | Enable noise gate (suppresses audio when no signal detected)          |
|              | `--eq2200 GAIN` | Apply gain at 2200 Hz in dB (use with `mw_cal` to find optimal value) |
|              | `--tx-delay MS` | Transmit preamble duration in milliseconds (default: 300)             |
|              | `--tx-tail MS`  | Transmit postamble duration in milliseconds (default: 50)             |

### Other
| Short option | Long option     | Description                               |
| ------------ | --------------- | ----------------------------------------- |
|              | `--exit-idle S` | Exit if no packets received for S seconds |
| `-v`         | `--verbose`     | Verbose logging                           |
| `-V`         | `--debug`       | Debug logging                             |

## Working principles

### Receive chain
Audio input → Channel EQ (optional) → Squelch gate (optional) → Demodulators → Dual bit-clock recovery (simple + PLL) → HDLC deframing → CRC deduplication → Output

#### Goertzel demodulators

Tone strengths calculated using Goertzel's algorithm, normalized and compared. 

For the "optimistic" variant, the "hyperparameters" have been procedurally optimized for [TNC Test CD by WA8LMF](http://wa8lmf.net/TNCtest/) track 1. For the "pessimistic", track 2 was used.

#### Quadrature demodulator

PM or FM-like algorithm. Mixes input with I/Q local oscillators at center frequency (1700 Hz), low-pass filters them, then calculates instantaneous frequency via phase difference (atan2). Output is normalized and clipped to produce soft symbols.

#### Split-filter demodulators

Two variants: mark-focused (low-pass at 2200 Hz) and space-focused (high-pass at 1200 Hz). Each applies envelope detection, post-filtering, and AGC normalization with independently tuned parameters for optimal performance.

#### RRC demodulator

Copied from Direwolf for experimentation, but disabled by default for performance reasons.

Uses Root-Raised-Cosine matched filtering with quadrature mixing via DDS oscillators for both mark and space frequencies. FIR filtering with RRC impulse response, envelope detection, and AGC normalization produce the final mark-space comparison.

#### Bit synchronization

Each demodulator feeds two bit-clock recovery algorithms running in parallel:

**Simple threshold-based:** Detects bit transitions by comparing consecutive samples. When the bit value changes, it resets timing to the middle of the bit period. Fast but sensitive to noise.

**PLL-based (preferred):** Uses a 32-bit phase accumulator with adaptive inertia for phase correction. Tracks zero-crossings with linear interpolation and maintains lock detection through transition timing analysis. Applies conservative corrections when locked (inertia 0.74) and aggressive corrections when searching (inertia 0.50). Hysteresis prevents lock flapping. Significantly more robust than the simple algorithm.

### Transmit chain

Input (stdin/TCP/UDP) → Protocol parsing (TNC2/KISS) → AX.25 framing → HDLC encoding (NRZI + bit stuffing) → Bell 202 FSK modulation (1200 Hz space, 2200 Hz mark) → Audio output

## Calibration

Use `mw_cal` to find the optimal `--eq2200` value for your radio:

```bash
mw_cal -d "hw:1,0" -r 48000
```

This displays real-time spectrum analysis with 8 frequency bins, using 1200 Hz as the reference (0 dB). Adjust whatever you've got available to try and make the 1200 and 2200 Hz bins equal. Otherwise, try to use `--eq2200` to compensate.

```bash
# If 2200 Hz shows -5 dB relative to 1200 Hz, add +5 dB boost
miniwolf -d "hw:1,0" -io -r 48000 --eq2200 5.0
```

## License

GNU General Public License v3.0 - see [LICENSE](LICENSE)

---

**Development notes:** See [.clinerules](.clinerules) for AI-friendly instructions.
