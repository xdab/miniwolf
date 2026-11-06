# data/

Real captures and test material. Gitignored and optional — the repo builds,
tests and runs every tool without it; drop files here when you have them.

The canonical material is the WA8LMF TNC Test CD: the CDDA image goes here,
with derived per-track files (44100 Hz stereo extracts plus 22050 Hz mono
bench inputs; filenames carry their format metadata). Track 1 is the clean
144.39 MHz APRS recording, track 2 adds degradation; both feed the
regression floors in `tools/regression.py`.

Decode counts and curve numbers are deliberately not recorded here — they
vary with the program and go stale. Measure fresh against whatever is
present: `./build/miniwolf_file -h` for direct decodes, `tools/ber.py` for
the synthetic reference sweep and demod comparison.
