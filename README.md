![Build](https://github.com/xdab/miniwolf/actions/workflows/c-cpp.yml/badge.svg)

# miniwolf

A soundcard modem/TNC for amateur radio packet communications: it encodes and decodes AX.25 packets over 1200 baud AFSK (Bell 202) and exchanges them with other tools over stdin/stdout, TCP, UDP, and Unix domain sockets.

> **Warning:** parts of the code and docs may be LLM-written.

## Why

A simple, lightweight alternative to the well-known and respected [Direwolf](https://github.com/wb2osz/direwolf), built around the idea of a focused tool that does one job well and lets other tools do the rest.

It is deliberately **not**:

- A drop-in replacement for Direwolf (configuration and CLI are not compatible)
- Cross-platform (it is built around ALSA, the Advanced **Linux** Sound Architecture)
- A multi-mode modem (only Bell 202 / 1200 baud AFSK, for now)
- A fully-featured APRS station (no built-in digipeating, beaconing, or APRS-IS connectivity)

For the missing pieces, pair it with dedicated tools. [APRX](https://github.com/PhirePhly/aprx) provides digipeating, cross-digipeating, beaconing, and bidirectional gating; [axdigi](https://github.com/xdab/axdigi) implements digipeating on its own; [aprsfmt](https://github.com/xdab/aprsfmt) formats APRS packets and, combined with `cron`, `netcat`, or `socat`, easily provides beaconing.

## Features

- AX.25 encode/decode over 1200 baud AFSK
- TNC2 or KISS framing on every interface layer
- stdin/stdout, TCP, UDP, and Unix domain socket interfaces
- Configurable pseudo-squelch, 2200 Hz equalization, TX delay and TX tail
- Built-in spectrum analyzer for audio level calibration

## Installation

Prerequisites:

- Linux with ALSA
- GCC or Clang
- CMake
- ALSA development libraries (`libasound2-dev` on Debian/Ubuntu)

```bash
git clone https://github.com/xdab/miniwolf.git
cd miniwolf
git submodule update --init --recursive
make release      # build
make install      # build in release mode and install system-wide
```

`make install` installs the `miniwolf` binary and the systemd service files from [systemd/](systemd/), then reloads the systemd daemon. `make build` produces a debug build instead. See the [Makefile](Makefile) for other targets (`test`, `bench`, `package`).

## Usage

```bash
# List available audio devices
miniwolf -l

# Receive and transmit with a TCP KISS server on port 8100
miniwolf -d "default" -io -r 44100 --tcp-kiss 8100

# Use a configuration file
miniwolf -c ~/miniwolf.conf

# Transmit a packet from stdin (TNC2 format)
echo "N0CALL>APRS:!4903.50N/07201.75W>test" | miniwolf -d "default" -o -r 44100
```

With no `--kiss` flag, stdin/stdout and all servers use TNC2 format (one packet per line).

## Command-line options

### Audio

| Short | Long           | Description                                   |
| ----- | -------------- | --------------------------------------------- |
| `-l`  | `--list`       | List audio devices and exit                   |
| `-d`  | `--dev=NAME`   | Audio device name (e.g. `default`, `hw:1,0`)  |
| `-r`  | `--rate=RATE`  | Sample rate in Hz (default: 44100)            |
| `-i`  | `--input`      | Enable audio input (receive)                  |
| `-o`  | `--output`     | Enable audio output (transmit)                |

### Protocol

| Short | Long             | Description                                                                    |
| ----- | ---------------- | ------------------------------------------------------------------------------ |
|       | `--kiss`         | Use KISS protocol instead of TNC2 for stdin/stdout                             |
| `-T`  | `--tnc2-extras`  | Send extra telemetry as comments on TNC2 sockets (except UDP)                  |

### Network

| Long                                            | Description                                 |
| ----------------------------------------------- | ------------------------------------------- |
| `--tcp-kiss PORT`                               | TCP server for KISS clients                 |
| `--tcp-tnc2 PORT`                               | TCP server for TNC2 clients                 |
| `--udp-kiss-addr ADDR` / `--udp-kiss-port PORT` | Send received packets via UDP (KISS)        |
| `--udp-tnc2-addr ADDR` / `--udp-tnc2-port PORT` | Send received packets via UDP (TNC2)        |
| `--udp-kiss-listen PORT`                        | Listen for KISS packets to transmit         |
| `--udp-tnc2-listen PORT`                        | Listen for TNC2 packets to transmit         |
| `--uds-kiss PATH`                               | Unix domain socket server for KISS packets  |
| `--uds-tnc2 PATH`                               | Unix domain socket server for TNC2 packets  |

### Signal processing

| Short | Long              | Description                                                            |
| ----- | ----------------- | ---------------------------------------------------------------------- |
| `-s`  | `--squelch=VAL`   | Pseudo-squelch strength, 0.0–1.0 (higher is stricter)                  |
| `-2`  | `--eq2200 GAIN`   | Equalization applied at 2200 Hz in dB (use `--calibrate` to tune)      |
| `-y`  | `--tx-delay MS`   | Preamble duration in milliseconds (default: 300)                       |
| `-z`  | `--tx-tail MS`    | Postamble duration in milliseconds (default: 30)                       |

### Other

| Short | Long              | Description                                    |
| ----- | ----------------- | ---------------------------------------------- |
| `-c`  | `--config=FILE`   | Read configuration from FILE                   |
| `-C`  | `--calibrate`     | Run the spectrum analyzer instead of the modem |
|       | `--exit-idle S`   | Exit if no packets received for S seconds      |
| `-x`  | `--noop`          | Do not enter the main processing loop          |
| `-v`  | `--verbose`       | Verbose logging                                |
| `-V`  | `--debug`         | Debug logging                                  |

## Configuration file

Configuration can optionally be read from a file with `-c FILE`.

The file uses `key=value` syntax with `#` comments. Keys mostly match the long option names; boolean options take `true`/`false`. CLI arguments override file entries.

```bash
# CLI
miniwolf -d "hw:1,0" -io -r 48000 --kiss -s 0.5 --eq2200 2.5 --tcp-tnc2 8101 --udp-tnc2-addr 127.0.0.1 --udp-tnc2-port 8001
```

is equivalent to:

```ini
# equivalent.conf
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

See [sample.conf](sample.conf) for a complete example.

## Working principles

Receive chain:

1. Audio input
2. Equalization
3. Squelch
4. Demodulation
5. Bit-clock recovery
6. HDLC deframing
7. Output (stdin/stdout, TCP, UDP, Unix domain socket)

Transmit chain:

1. Input (stdin/stdout, TCP, UDP, Unix domain socket)
2. Protocol parsing (TNC2 or KISS)
3. AX.25 encoding
4. HDLC framing
5. FSK modulation
6. Audio output

The protocol implementations (AX.25, HDLC, KISS, TNC2, CRC-CCITT) live in the [libtnc](libs/libtnc) git submodule.

## Calibration

Use the built-in spectrum analyzer to find the optimal `--eq2200` value for your radio:

```bash
miniwolf -C -d "hw:1,0" -r 48000
```

This displays a real-time spectrum analysis with 8 frequency bins, using 1200 Hz as the reference (0 dB). Adjust whatever you have available to make the 1200 Hz and 2200 Hz bins equal, or compensate with `--eq2200`:

```bash
# If 2200 Hz shows -5 dB relative to 1200 Hz, add +5 dB boost
miniwolf -d "hw:1,0" -io -r 48000 --eq2200 5.0
```

## TCP audio bridge (optional)

miniwolf reads audio only from an ALSA capture device. To feed it samples from a network stream, use the bundled bridge: it connects to a raw mono `f32le` TCP audio stream, resamples it, and writes it into an ALSA loopback device that miniwolf captures from.

```bash
sudo modprobe snd-aloop
# terminal 1
/usr/local/bin/mw-audio-bridge.sh          # or: systemctl start mw-audio-bridge
# terminal 2
miniwolf -d plughw:Loopback,1,0 -i -r 48000
```

The script exits on any connection problem or idle timeout, so `mw-audio-bridge.service` (installed by `make install`, configured via its `Environment=` lines) can restart it automatically. Host, port, input/output rates, and idle timeout are set through `MW_AUDIO_*` environment variables — see the script header. The unit works both system-wide and as a user service (`systemctl --user`); details in [systemd/README.md](systemd/README.md).

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
