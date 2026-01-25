![Build](https://github.com/xdab/miniwolf/actions/workflows/c-cpp.yml/badge.svg)

# miniwolf

_Danger! Parts of the code and docs may be LLM-written._

### What it is

Soundcard modem/TNC for amateur radio packet communications designed as a simple, lightweight alternative to well-known and respected [Direwolf by WB2OSZ](https://github.com/wb2osz/direwolf).

It supports encoding and decoding AX.25 packets over 1200 baud AFSK while interfacing with existing tools over stdin/stdout, TCP as well as UDP. On each of these "interfacing layers" both TNC2 and KISS can be used.

### What it isn't

This project is **not** a:

- Drop-in replacement for Direwolf
  - Configuration files and command line arguments are not compatible
- Cross-platform program
  - It is built around ALSA, the Advanced **Linux** Sound Architecture, the implication is fairly obvious
- Multi-mode modem (for now)
  - Only Bell202 / 1200 baud AFSK is supported
    - hardcoded actually...
- Full-featured APRS station
  - No built-in digipeating, beaconing, APRS-IS connectivity
  
This **is the whole point!** To have a focused tool that does one job well. Let other tools figure out the rest.

## Build and installation

While cloning the project is entirely standard, the building differs slightly from other projects using CMake. A root-level Makefile is specified with some shortcuts for building and even installing.

### Prerequisites

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

# Use configuration file
miniwolf -c ~/miniwolf.conf
```

## Command line arguments

### Audio setup

| Short option | Long option         | Description                                   |
| ------------ | ------------------- | --------------------------------------------- |
| `-l`         | `--list`            | List audio devices and exit                   |
| `-d NAME`    | `--dev=NAME`        | Audio device name (e.g., "default", "hw:1,0") |
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
| `-s VAL`     | `--squelch=VAL` | Squelch strength (float, e.g. 0.5)                                    |
|              | `--eq2200 GAIN` | Apply gain at 2200 Hz in dB (use with `mw_cal` to find optimal value) |
|              | `--tx-delay MS` | Transmit preamble duration in milliseconds (default: 300)             |
|              | `--tx-tail MS`  | Transmit postamble duration in milliseconds (default: 50)             |

### Other

| Short option | Long option     | Description                               |
| ------------ | --------------- | ----------------------------------------- |
|              | `--exit-idle S` | Exit if no packets received for S seconds |
| `-v`         | `--verbose`     | Verbose logging                           |
| `-V`         | `--debug`       | Debug logging                             |

## Configuration file

Optionally, configuration can be read from a file using `-c FILE` or `--config=FILE`.

The file uses simple `key=value` syntax with `#` comments.
Keys in the file are the same as long option names of CLI arguments.

File-based configuration is secondary to CLI arguments.
Arguments override entries in the configuration file.

### Example

```bash
# CLI
miniwolf -d "hw:1,0" -io -r 48000 -k -s 0.5 --eq2200 2.5 --tcp-tnc2 8101 --udp-tnc2-addr 127.0.0.1 --udp-tnc2-port 8001
```

is equivalent to:

```ini
# Equivalent.conf
dev=hw:1,0
input=true
output=true
rate=48000
kiss=true
squelch=0.5
eq2200=2.5
tcp-tnc2=8101
udp-tnc2-addr=127.0.0.1
udp-tnc2-port=8001
```

## Working principles

### Signal chain

**Receive:** Audio input → EQ → Squelch → Demodulators → Bit-clock recovery → HDLC deframe → Output

**Transmit:** Input (stdin/TCP/UDP) → Protocol parse → AX.25 → HDLC encode → FSK modulate → Audio output

Protocol implementation (AX.25, HDLC, KISS, TNC2, CRC-CCITT) resides in [libtnc](libs/libtnc/) git submodule.

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
