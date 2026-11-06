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

  Decode counts and baselines are NOT recorded here — they vary with the
  program and go stale (see the policy in TODOS.md). Measure fresh:

  - Real captures: `./build/miniwolf_file -f data/tnctestNN_22050Hz_mono_S16LE.raw -F S16 -r 22050`
  - Synthetic reference sweep: `python3 tools/ber.py --cnr-min 4 --cnr-max 16 --cnr-step 1`
  - Demod comparison: same sweep with `--demods` solo arms and race arms

  Track content per the CD's `_READme.htm`: track 1 = clean 144.39 MHz APRS
  recording; tracks 2+ add distance/degradation; track 5-7 are short
  special-case tests (collisions, HT low-power, etc.). Tracks 1 and 2 are
  the ones that matter; the rest is secondary.

- Synthetic static-channel reference (tools/ber.py, no fading/CFO/baud
  error, 48 kHz, 30-char UI frames, seed 1, noise scaled to the keyed-frame
  carrier at the stated C/N). Qualitative shape of the curve on the current
  chain (digits intentionally omitted; regenerate as above):

  - A knee around single-digit-dB C/N at the IF output, steep on both
    sides; clean decodes a few dB above the knee.
  - All failures are `none` (no output), never `wrong`: no false decodes
    at any C/N.
  - Theory reference: `tools/ber_theory.py` (Rice click-noise model +
    noncoherent FSK, same framing) lands near the measured knee, with the
    measured receiver slightly better, consistent with detector
    integration smoothing the model's all-or-nothing click flips.

  Demodulator ranking (same sweep, `--demods` solo arms): quadrature is
  the best solo demodulator at every C/N; the two Goertzel tunings are
  near-identical to each other and slightly behind quadrature. In the
  default race the output is dominated by goertzel-optim by first-wins
  order, quadrature contributing knee rescues, goertzel-pesim practically
  none despite completing as many frames as optim. The race beats every
  solo arm.
