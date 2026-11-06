#!/usr/bin/env python3
"""Bell202 AFSK frame generator: AX.25 packet -> raw audio file.

Builds a full HDLC-framed packet (flag preamble, bit-stuffed frame + FCS,
flag tail), NRZI-encodes it, and modulates it as phase-continuous Bell202
AFSK (mark 1200 Hz for tone-bit 1, space 2200 Hz for tone-bit 0, LSB first).
It only modulates — the agreeing demodulator is the miniwolf bench binary
(src/main_file.c), e.g.:

    python3 tools/frame.py --text "hello world" -o frame.f32
    build/miniwolf_file -f frame.f32 -r 48000

The output is float32 little-endian, one channel. Ground truth (the packet
in TNC2 form) goes to stdout, one line per frame, so a gate can compare it
against the binary's decode.

Modulation matches src/mod.c and src/synth.c: one free-running phase
accumulator across the whole transmission (--phase-reset resets it per
symbol, for A/B against a non-continuous transmitter).

The channel model lives in tools/channel.py and stacks: --cfo shifts the
AFSK tone pair, --mobile adds VHF picket fencing, --cnr adds receiver
thermal noise as C/N in dB at the IF filter's output. The TX clock error is
not a channel arm: --baud 1200.3 simply transmits at that clock. Everything
stochastic is driven by --seed, so paired A/B runs (same seed, only the
receiver differs) see identical randomness.
"""

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import channel

MARK_HZ = 1200.0
SPACE_HZ = 2200.0
BAUD = 1200.0
FLAG = 0x7E


# Verbatim from libs/libtnc/include/crc.h (the reflected 0x8408 table).
CRC_CCITT_TABLE = [
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78,
]


def crc_ccitt(data: bytes) -> int:
    """Byte-for-byte port of libtnc crc.c: init 0xffff,
    crc = (crc >> 8) ^ TABLE[(crc ^ byte) & 0xff]; get() = crc ^ 0xffff.
    This is the reflected (AX.25/KERMIT) CRC, not MSB-first CCITT-FALSE."""
    crc = 0xFFFF
    for byte in data:
        crc = (crc >> 8) ^ CRC_CCITT_TABLE[(crc ^ byte) & 0xFF]
    return crc ^ 0xFFFF


def ax25_address(callsign: str, ssid: int, last: bool) -> bytes:
    """One 7-byte AX.25 address field: shifted callsign + SSID byte."""
    cs = callsign.upper().ljust(6)
    if len(cs) > 6:
        raise ValueError(f"callsign too long: {callsign}")
    ssid_byte = 0x60 | ((ssid & 0x0F) << 1) | (0x01 if last else 0x00)
    return bytes(ord(c) << 1 for c in cs) + bytes([ssid_byte])


def ax25_frame(dest: str, source: str, digis: list[str], text: str) -> bytes:
    """Full AX.25 UI frame: addresses, control 0x03, PID 0xF0, info."""
    addrs = ax25_address(dest, 0, False) + ax25_address(source, 0, len(digis) == 0)
    for i, d in enumerate(digis):
        callsign, _, ssid = d.partition("-")
        addrs += ax25_address(callsign, int(ssid or 0), i == len(digis) - 1)
    return addrs + bytes([0x03, 0xF0]) + text.encode("ascii")


def hdlc_bits(frame: bytes, head_flags: int, tail_flags: int) -> np.ndarray:
    """HDLC framing + bit stuffing + NRZI, mirroring libtnc hldc.c:

    flags pass through unstuffed, everything between (frame bytes, then FCS
    little-endian) is stuffed (a 0 after five 1s) and NRZI-encoded (tone bit
    toggles on data bit 0), all LSB first, initial NRZI level 0.
    """
    # everything from here on is DATA bits, LSB first; flags pass through
    # unstuffed, everything between (frame bytes, then FCS) is bit-stuffed,
    # mirroring libtnc hldc.c
    flag_bits = [(FLAG >> i) & 1 for i in range(8)]
    data_bits = flag_bits * head_flags

    # FCS = crc_ccitt() (already the complemented on-air value), LE
    crc = crc_ccitt(frame)
    fcs_frame = frame + bytes([crc & 0xFF, crc >> 8])
    ones = 0
    for byte in fcs_frame:
        for i in range(8):
            bit = (byte >> i) & 1
            data_bits.append(bit)
            ones = ones + 1 if bit else 0
            if ones == 5:
                data_bits.append(0)
                ones = 0
    data_bits += flag_bits * tail_flags

    if os.environ.get("FRAME_DEBUG"):
        dbits = "".join(f"{b:08b}"[::-1] for b in fcs_frame)
        print(f"[py] frame {len(frame)}B: {frame.hex(' ')}", file=sys.stderr)
        print(f"[py] fcs {crc:04X}, fcs_frame {len(fcs_frame)}B", file=sys.stderr)
        print(f"[py] data bits {len(data_bits)}: {''.join(map(str, data_bits))}", file=sys.stderr)

    # NRZI: data bit 0 toggles the tone bit (libtnc nrzi.h, initial level 0)
    nrzi = np.empty(len(data_bits), dtype=np.uint8)
    level = 0
    for j, bit in enumerate(data_bits):
        if not bit:
            level ^= 1
        nrzi[j] = level
    return nrzi


def modulate(bits: np.ndarray, rate: float, baud: float, mark: float,
             space: float, amplitude: float, phase_reset: bool) -> np.ndarray:
    """Literal port of src/mod.c + src/synth.c: per bit,
    num_samples = (int)(rate/baud); omega = 2*pi*f/rate; out[i] = sin(phase);
    phase += omega; phase wrapped with fmod(2*pi) every sample. Tone bit 1 ->
    mark, 0 -> space; one free-running phase accumulator across the whole
    transmission, or per-symbol phase reset with --phase-reset.
    """
    out = np.empty(len(bits) * (int(rate / baud)), dtype=np.float64)
    pos = 0
    phase = 0.0
    for bit in bits:
        n = int(rate / baud)
        omega = 2.0 * np.pi * (mark if bit else space) / rate
        if phase_reset:
            phase = 0.0  # restart at each symbol boundary (hard phase jumps)
        for _ in range(n):
            out[pos] = np.sin(phase)
            pos += 1
            phase += omega
            phase %= 2.0 * np.pi
    return (amplitude * out).astype(np.float32)


def tnc2(dest: str, source: str, digis: list[str], text: str) -> str:
    """Ground-truth TNC2 monitor string (source>dest via digis: payload)."""
    path = ",".join(digis)
    return f"{source.upper()}>{dest.upper()}{',' + path if path else ''}:{text}"


def main() -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-o", "--out", default="frame.f32", help="output raw f32le file (default: frame.f32)")
    p.add_argument("--text", default="test packet", help="packet payload text (default: 'test packet')")
    p.add_argument("--dest", default="APZMWL", help="destination callsign (default: APZMWL)")
    p.add_argument("--source", default="N0CALL", help="source callsign (default: N0CALL)")
    p.add_argument("--digi", action="append", default=[], help="digipeater call[-ssid], repeatable, in path order")
    p.add_argument("-r", "--rate", type=float, default=48000.0, help="sample rate Hz (default: 48000)")
    p.add_argument("--baud", type=float, default=BAUD, help="TX baud rate — set 1200.3 for a clock-error TX (default: 1200)")
    p.add_argument("--mark", type=float, default=MARK_HZ, help="mark Hz for tone bit 1 (default: 1200)")
    p.add_argument("--space", type=float, default=SPACE_HZ, help="space Hz for tone bit 0 (default: 2200)")
    p.add_argument("--amplitude", type=float, default=0.5, help="peak amplitude 0..1 (default: 0.5)")
    p.add_argument("--head-flags", type=int, default=16, help="flag bytes before the frame (default: 16)")
    p.add_argument("--tail-flags", type=int, default=4, help="flag bytes after the frame (default: 4)")
    p.add_argument("--pre-silence", type=float, default=0.2, help="seconds of silence before TX (default: 0.2)")
    p.add_argument("--post-silence", type=float, default=0.2, help="seconds of silence after TX (default: 0.2)")
    p.add_argument("--frames", type=int, default=1,
                   help="number of frames to emit, separated by --gap of "
                        "channel silence (default: 1); text gains a ' #k' "
                        "suffix per frame so decodes match truth lines 1:1")
    p.add_argument("--gap", type=float, default=1.0,
                   help="seconds of channel silence between --frames frames "
                        "(default: 1.0); the impairment arms keep the noise "
                        "and fading alive in the gap")
    p.add_argument("--phase-reset", action="store_true",
                   help="reset phase at each symbol (hard jumps) instead of continuous phase")
    p.add_argument("--cnr", type=float, default=None, metavar="DB",
                   help="receiver thermal noise as C/N in dB at the IF "
                        "filter's output (tools/channel.py); without "
                        "--mobile this is the static-station reference "
                        "channel")
    p.add_argument("--cfo", type=float, default=None, metavar="HZ",
                   help="AFSK tone-pair offset from the nominal 1200/2200 Hz "
                        "in Hz (default: none)")
    p.add_argument("--mobile", type=float, default=None, metavar="KPH",
                   help="VHF mobile fading — picket fencing (tools/channel.py): "
                        "vehicle speed; 0 = parked in multipath")
    p.add_argument("--mobile-k", type=float, default=0.0, dest="mobile_k",
                   metavar="K", help="Rician K factor for --mobile, 0 = "
                   "Rayleigh, large = open-road LOS")
    p.add_argument("--mobile-fc", type=float, default=144.8e6, dest="mobile_fc",
                   help="RF carrier Hz (default: 144.8 MHz, NA APRS)")
    p.add_argument("--mobile-deviation", type=float, default=3000.0,
                   dest="mobile_deviation", metavar="HZ",
                   help="FM deviation Hz (default: 3000)")
    p.add_argument("--mobile-paths", type=int, default=50, dest="mobile_paths",
                   metavar="N", help="multipath scatterer count (default: 50)")
    p.add_argument("--mobile-if-bw", type=float, default=25000.0,
                   dest="mobile_if_bw", metavar="HZ",
                   help="IF/channel bandwidth Hz; 12.5/20/25 kHz NFM, "
                        "0 disables (default: 25000)")
    p.add_argument("--mobile-limiter", type=float, default=3.0,
                   dest="mobile_limiter", metavar="X",
                   help="discriminator clamp in deviation units (default: 3)")
    p.add_argument("--mobile-ptt-gate", type=float, default=0.0025,
                   dest="mobile_ptt_gate", metavar="S",
                   help="TX keying envelope window in seconds; 0 keeps the "
                        "carrier permanently on (default: 0.0025)")
    p.add_argument("--seed", type=int, default=0,
                   help="RNG seed for noise and fading; identical seeds draw "
                        "identical channels across runs (paired A/B) "
                        "(default: 0)")
    args = p.parse_args()

    digis = [d for spec in args.digi for d in spec.split(",")]
    pre = np.zeros(int(args.pre_silence * args.rate), dtype=np.float32)
    post = np.zeros(int(args.post_silence * args.rate), dtype=np.float32)
    gap = np.zeros(int(args.gap * args.rate), dtype=np.float32)

    # N frames separated by channel silence; " #k" suffix (frame 1 plain) so
    # each decode matches one truth line 1:1; gaps carry the same channel
    pieces = []
    texts = []
    for k in range(args.frames):
        text = args.text if args.frames == 1 else f"{args.text} #{k + 1}"
        texts.append(text)
        if k:
            pieces.append(gap)
        frame = ax25_frame(args.dest, args.source, digis, text)
        bits = hdlc_bits(frame, args.head_flags, args.tail_flags)
        pieces.append(modulate(bits, args.rate, args.baud, args.mark,
                               args.space, args.amplitude, args.phase_reset))
    audio = np.concatenate([pre] + pieces + [post])

    for text in texts:
        print(tnc2(args.dest, args.source, args.digi, text))

    # channel: tone offset first, then the FM link (tools/channel.py);
    # --mobile omitted = static reference, --cnr omitted = pure fading
    rng = np.random.default_rng(args.seed)
    if args.cfo is not None:
        audio = channel.cfo_offset(audio, args.rate, args.cfo)
    if args.cnr is not None or args.mobile is not None:
        audio = channel.mobile_channel(
            audio, args.rate, rng, fc=args.mobile_fc,
            speed_kph=args.mobile if args.mobile is not None else 0.0,
            k=args.mobile_k, cnr_db=args.cnr,
            deviation=args.mobile_deviation, paths=args.mobile_paths,
            limiter=args.mobile_limiter, ptt_window_s=args.mobile_ptt_gate,
            if_bw=args.mobile_if_bw)

    with open(args.out, "wb") as f:
        f.write(audio.tobytes())

    dur = len(audio) / args.rate
    chan = " ".join(f"{k}={v}" for k, v in (
        ("cfo", args.cfo), ("baud", args.baud if args.baud != BAUD else None),
        ("mobile", args.mobile), ("K", args.mobile_k), ("cnr", args.cnr),
        ("ifbw", args.mobile_if_bw)) if v is not None) or "clean"
    print(f"{args.frames} frame(s), {dur:.3f} s, channel {chan}, {args.out}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
