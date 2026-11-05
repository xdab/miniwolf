#!/usr/bin/env python3
"""Slice a CUE/BIN CD image into per-track WAV files.

Audio tracks are raw CDDA (44100 Hz, stereo, S16 LE) at 2352-byte sector
boundaries; this reads the cue sheet's INDEX 01 marks, slices the BIN between
them, and pipes each slice to ffmpeg to write a WAV.

Usage: python3 tools/cue_slice.py <image.cue> <outdir>
"""

import os
import re
import subprocess
import sys

SECTOR = 2352


def sectors(msf: str) -> int:
    mm, ss, ff = map(int, msf.split(":"))
    return (mm * 60 + ss) * 75 + ff


def main() -> int:
    cue, outdir = sys.argv[1], sys.argv[2]
    text = open(cue).read()

    entries = []  # (track_no, start_sector, end_sector_or_None)
    lead_out = None
    for m in re.finditer(r"TRACK (\d+) (\w+/\d+|\w+)|INDEX (\d+) (\d+:\d+:\d+)|REM LEAD-OUT (\d+:\d+:\d+)", text):
        if m.group(1):
            entries.append([int(m.group(1)), m.group(2), None, None])
        elif m.group(3) == "01":
            entries[-1][2] = sectors(m.group(4))
        elif m.group(5):
            lead_out = sectors(m.group(5))

    for i, e in enumerate(entries):
        e[3] = entries[i + 1][2] if i + 1 < len(entries) else lead_out

    binfile = os.path.join(os.path.dirname(os.path.abspath(cue)),
                           re.search(r'FILE "([^"]+)"', text).group(1))
    for no, fmt, start, end in entries:
        if not fmt.startswith("AUDIO"):
            continue
        raw = f"{outdir}/track{no:02d}.raw"
        wav = f"{outdir}/track{no:02d}.wav"
        with open(binfile, "rb") as f:
            f.seek(start * SECTOR)
            data = f.read((end - start) * SECTOR)
        with open(raw, "wb") as g:
            g.write(data)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error",
                        "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", raw,
                        "-c:a", "pcm_s16le", wav], check=True)
        print(f"track{no:02d}: {end - start} sectors, {((end - start) * SECTOR) / (44100 * 4):.1f} s -> {wav}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
