![Build](https://github.com/xdab/miniwolf/actions/workflows/c-cpp.yml/badge.svg)

# miniwolf

**A minimalist soundcard modem/TNC for amateur radio.** Lightweight Direwolf replacement supporting Bell 202 (1200 bps) packet radio and APRS with multiple demodulation strategies, TCP servers, and offline processing.

## Features

- **Multiple demodulation strategies** (Goertzel, quadrature, split-filter) - up to 6 parallel demodulators for robust reception
- **Full AX.25 protocol support** with TNC2 (text) and KISS (binary) formats
- **TCP servers** (separate KISS and TNC2 ports) and UDP transmission for integration with existing tools
- **Real-time and offline processing** - live soundcard or pre-recorded audio files
- **Advanced DSP** - AGC, multi-order Butterworth filters, FFT spectrum analysis
- **Configurable squelch, channel EQ, and TX timing** via CLI arguments
- **Cross-platform** (Linux/macOS) with ALSA for audio I/O
- **Small footprint** - optimized for embedded/low-power systems with 32-bit float arithmetic
- **Static binaries** - no runtime config files needed

## Quick Start

```bash
# Build
git clone https://github.com/xdab/miniwolf.git
cd miniwolf
make build

# List audio devices
./build/src/miniwolf --list

# Start TNC (receive-only)
./build/src/miniwolf --dev "default" --input --rate 48000

# Or with TCP KISS server on port 8100
./build/src/miniwolf --dev "default" --input --output --rate 48000 --tcp-kiss 8100
```

## Usage

### Main TNC Application

```
miniwolf [OPTIONS]
```

**Key options:**

- `-l, --list` - List audio devices
- `-d NAME, --dev=NAME` - Audio device
- `-r RATE, --rate=RATE` - Sample rate (Hz)
- `-i, --input` - Enable input
- `-o, --output` - Enable output
- `-k, --kiss` - KISS mode (instead of TNC2)
- `--tcp-kiss PORT` - TCP KISS server
- `--tcp-tnc2 PORT` - TCP TNC2 server
- `--udp-kiss-addr ADDR` - UDP destination address for KISS packets
- `--udp-kiss-port PORT` - UDP destination port for KISS packets
- `--udp-tnc2-addr ADDR` - UDP destination address for TNC2 packets
- `--udp-tnc2-port PORT` - UDP destination port for TNC2 packets
- `--udp-kiss-listen PORT` - UDP server port for receiving KISS packets
- `--udp-tnc2-listen PORT` - UDP server port for receiving TNC2 packets
- `-s, --squelch` - Enable noise gate
- `--eq2200 GAIN` - Channel EQ at 2200 Hz (dB)
- `--tx-delay MS` - TX pre-amble duration (default 300ms)
- `--tx-tail MS` - TX post-amble duration (default 50ms)
- `--exit-idle S` - Exit program if no packets received in S seconds
- `-v, --verbose` / `-V, --debug` - Logging levels

### Offline Demodulation

Process pre-recorded audio files:

```bash
./build/mw_bench --file recording.raw --rate 48000 --format F32
```

Supports multiple formats: F32, F64, S8, S16, S32, U8, U16, U32 (with LE/BE byte order)

### Spectrum Analyzer

Real-time audio spectrum display:

```bash
./build/mw_cal --device "hw:1,0"
```

Shows FFT bins with Bell 202 frequencies (1200/2200 Hz) highlighted.

### RF Traffic Logger

Log all received packets to date-stamped files:

```bash
# Log from TCP KISS server running on localhost:8100
./build/mw_log --tcp-addr 127.0.0.1 --tcp-port 8100 --prefix rx

# Or listen on UDP port for KISS packets
./build/mw_log --udp-port 18144 --kiss --prefix rx

# TNC2 format (default)
./build/mw_log --tcp-addr 127.0.0.1 --tcp-port 8144 --prefix rx
```

**Logger options:**

- `--tcp-addr ADDR` - TCP server address to connect to
- `--tcp-port PORT` - TCP server port
- `--udp-port PORT` - UDP listening port for receiving packets
- `--prefix STR` - Log file prefix (default: "rx", creates files like `rx-20260114.log`)
- `-k, --kiss` - Parse KISS binary format (default: TNC2 text)
- `-v, --verbose` / `-V, --debug` - Logging levels

## How It Works

**Reception:**
Audio input → Channel EQ (optional) → Squelch (optional) → Up to 6 parallel demodulators → Dual bit-clock recovery (simple + PLL) → HDLC deframing → Deduplication → Output (TNC2/KISS via stdout or TCP)

**Transmission:**
Input (stdin/TCP) → TNC2/KISS parsing → AX.25 packing → HDLC framing → FSK modulation → Audio output

All code uses static buffers and 32-bit floats; callback audio thread has no malloc/free/locks.

## Build & Development

```bash
make build           # Debug build
make release         # Optimized release build
make test            # Run unit tests
make clean           # Remove build artifacts
```

**Libraries** (built independently):

- `libax25.a` - Protocol stack (AX.25, KISS, TNC2, HDLC, CRC)
- `libdsp.a` - DSP (AGC, FFT, filters, Goertzel, synthesis)
- `libminiwolf_core.a` - Core infrastructure (modem, demod, ring buffers, TCP)

**Executables:**

- `miniwolf` - Main TNC application
- `mw_bench` - Offline demodulator
- `mw_cal` - Spectrum analyzer
- `mw_log` - RF traffic logger (receives from TCP/UDP, logs to files)
- `mw_test` - Unit tests

## Requirements

- Linux or macOS
- ALSA development libraries (libasound2-dev)
- CMake 3.10+, Make, C compiler (GCC/Clang)

## License

GNU General Public License v3.0 - see [LICENSE](LICENSE)

---

**For detailed documentation, see [.clinerules](.clinerules) for development notes or explore the source code.**
