# systemd units

## miniwolf.service

Runs the TNC itself against a radio attached to the default ALSA device. Command line and defaults are embedded in the unit's `ExecStart` — edit it to match your setup, then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now miniwolf
```

## mw-loopback.service

Loads the `snd-aloop` kernel module. It is a root-level `Type=oneshot` unit with `RemainAfterExit=yes`, so it runs once at boot and simply exits — the loopback device stays available for anything that wants it. `modprobe` is idempotent: if the module is already loaded (or built into the kernel), it succeeds without doing anything.

This exists as a separate unit because user-level services cannot load kernel modules and, being in a different systemd manager, cannot formally depend on system units. Enable it once as root, and both system and user bridges can consume the device at will:

```bash
sudo systemctl enable --now mw-loopback
```

To skip the module load entirely at boot (preferred alternative), blacklist-style persistence works too: `echo snd-aloop | sudo tee /etc/modules-load.d/snd-aloop.conf` — the unit tolerates an already-loaded module either way.

## mw-audio-bridge.service + mw-audio-bridge.sh

miniwolf has no network audio input; it captures samples only from an ALSA device. This bridge feeds it from a TCP stream instead:

```
TCP server (raw mono f32le) -> nc -> ffmpeg (resample) -> aplay -> snd-aloop playback end
miniwolf <- snd-aloop capture end
```

The script:

1. Checks that a loopback device exists (exits if not — enable `mw-loopback.service` first)
2. Connects with `nc -w IDLE_TIMEOUT` — the connection is dropped after that many seconds without data
3. Resamples the stream from `MW_AUDIO_IN_RATE` to `MW_AUDIO_OUT_RATE` with ffmpeg. This must happen in userspace: `snd-aloop` is a dumb sample pipe, its two ends must run at the same rate or audio comes out pitch-shifted
4. Writes raw `f32le` samples into `plughw:Loopback,0,0` (the loopback's playback end); miniwolf captures from the paired `plughw:Loopback,1,0`

`set -euo pipefail` means any stage failing (connection refused, reset, EOF, ffmpeg/aplay death, device gone) exits the script — and so does a clean disconnect. Combined with the unit's `Restart=always` / `RestartSec=5`, the bridge self-heals: it comes back within 5 seconds until the stream is usable again. This also covers the boot-order edge case: if the bridge starts before `mw-loopback.service` has run, it exits with a clear message and retries until the device appears.

Configuration via `Environment=` lines in the unit (or exported variables when run by hand):

| Variable                | Default   | Meaning                                        |
| ----------------------- | --------- | ---------------------------------------------- |
| `MW_AUDIO_HOST`         | 127.0.0.1 | Audio server address                           |
| `MW_AUDIO_PORT`         | 7355      | Audio server port                              |
| `MW_AUDIO_IN_RATE`      | 16000     | Sample rate of the TCP stream                  |
| `MW_AUDIO_OUT_RATE`     | 48000     | Rate miniwolf captures at (both loopback ends) |
| `MW_AUDIO_IDLE_TIMEOUT` | 30        | Seconds without data before exiting (`nc -w`)  |

Notes:

- If the server only streams while transmitting, raise `MW_AUDIO_IDLE_TIMEOUT` or the bridge will restart continuously during silence
- Input format is assumed raw mono `f32le`; for `s16le` streams change `-f f32le` on ffmpeg's input side (the demodulator does not care about the absolute scale)
- Start miniwolf before the bridge, or the first moments of a stream may be lost while the loopback blocks

Install (done automatically by `make install`):

```bash
sudo install -m 755 mw-audio-bridge.sh /usr/local/bin/
sudo cp *.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mw-loopback
sudo systemctl enable --now mw-audio-bridge
```

### Running as a user service

The bridge unit is written to work identically with `systemctl --user` — no root required for the bridge itself:

```bash
sudo systemctl enable --now mw-loopback      # once, as root
mkdir -p ~/.config/systemd/user
cp mw-audio-bridge.service ~/.config/systemd/user/
# adjust Environment= lines and, if the script is not in /usr/local/bin, ExecStart=
systemctl --user daemon-reload
systemctl --user enable --now mw-audio-bridge
```

One thing the user manager cannot do for you:

- **Staying alive after logout** — user services are stopped when the last session closes unless lingering is enabled:
  ```bash
  sudo loginctl enable-linger $USER
  ```

If capture fails with a permission error, add yourself to the `audio` group (`sudo usermod -aG audio $USER`, re-login afterwards).
