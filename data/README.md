# data/

Real captures and test material. Gitignored — everything here is fetched or
dropped in locally.

Current contents (place files as listed):

- WA8LMF TNC Test CD (TNC_Test_Ver-1.1.cue/.bin, gitignored). Regenerate the
  derived files with `python3 tools/cue_slice.py data/TNC_Test_Ver-1.1.cue data`
  plus the ffmpeg resample below. Filenames carry their format metadata.

  - `trackNN_44100Hz_stereo_S16LE.wav/.raw` — the CDDA audio tracks as
    extracted, 44100 Hz stereo S16 LE (the .raw and .wav hold identical PCM).
  - `tnctestNN_22050Hz_mono_S16LE.raw` — bench input: mono 22050 Hz S16 LE,
    produced with
    `ffmpeg -f wav -i trackNN_44100Hz_stereo_S16LE.wav -ar 22050 -ac 1 -c:a pcm_s16le -f s16le tnctestNN_22050Hz_mono_S16LE.raw`
    (decode: `./build/miniwolf_file -f data/tnctestNN_22050Hz_mono_S16LE.raw -F S16 -r 22050`).

  Measured decode counts (miniwolf_file, current demods; the demod commit messages
  cite hand-tuned targets of 907/980/1007 packets — see TODOS.md). Tracks 1
  and 2 are the ones that matter; the rest is secondary:
  track01: 1010, track02: 934, track03: 100, track04: 98, track05-07: 0.

  Track content per the CD's `_READme.htm`: track 1 = clean 144.39 MHz APRS
  recording; tracks 2+ add distance/degradation; track 5-7 are short
  special-case tests (collisions, HT low-power, etc.).

- Synthetic static-channel reference (tools/ber.py, no fading/CFO/baud
  error, 48 kHz, 30-char UI frames, seed 1). Measured on the current chain:

  - PER 50% at 4.0 dB C/N at the IF output; 15% at 5 dB; 3% at 6 dB
    (300 trials/point).
  - Clean (0/300) from 7 dB upward; flat 0/5000 across 20-28 dB
    (1000 trials/point).
  - All failures are `none` (no output), never `wrong`: zero false decodes
    at any C/N. Below-threshold survivors decay exponentially (1/300 at 2 dB).

  This is the baseline every future sweep A/Bs against; regenerate with
  `python3 tools/ber.py --cnr-min 0 --cnr-max 10 --cnr-step 1 --trials 300`.

  Demodulator ranking (same sweep, 300 trials/point, `--demods` solo arms):
  quadrature is the best solo demodulator at every C/N (PER 55% vs 61%
  optim / 62% pesim at 4 dB; 17% vs 23% at 5 dB); the two Goertzel tunings
  are near-identical to each other and ~0.5 dB behind quadrature. In the
  default race the output is dominated by goertzel-optim by first-wins
  order (300/300 wins from 6 dB), quadrature contributing knee rescues
  (21/300 wins at 4 dB), goertzel-pesim practically none (6) despite
  completing as many frames as optim.
