# Audio Subsystem

Current-state reference for how `miniwolf` captures audio from ALSA, routes
it through the system, and plays audio back. Covers lifecycle, buffer
ownership, and error recovery — without reference to any protocol-specific
processing. Defined in `src/audio.c` / `include/audio.h`.

## Overview

- Capture of mono, native-endian 32-bit float, interleaved PCM from an ALSA device.
- Playback of the same format to the same or a different ALSA device.
- A lock-free SPSC ring buffer (`src/ring.c`) decoupling the playback source from the ALSA write path.
- A poll file descriptor integrating capture readiness into the main event loop (`src/loop.c`).

Every block of samples travels as a `float_buffer_t`
(`libs/libtnc/include/buffer.h`): `{ float *data; int capacity; int size; }`.

## Lifecycle

Called in order from `main()` (`src/main.c`), then per-iteration by `loop_run()` (`src/loop.c`):

1. **`aud_initialize()`** — allocates the playback ring, capacity
   `RING_BUFFER_SIZE` = 131072 samples (~2.7 s at 48 kHz).
2. **`aud_configure(device, rate, do_input, do_output)`** — opens the PCM
   stream(s) and applies hardware params via `aud_hw_params_apply()`:
   interleaved RW, `SND_PCM_FORMAT_FLOAT`, mono, rate-near, period
   `ALSA_PERIOD_SIZE` = 4096 frames, buffer 4 periods.
3. **`aud_get_poll_fd()`** — capture fd via `snd_pcm_poll_descriptors()`,
   registered with the application poller.
4. **`aud_start()`** — prepares and starts capture; playback is prepared
   only, fed on demand.
5. **`aud_terminate()`** — closes PCM handles, destroys the ring.

## Capture path

Capture is exposed to the application through the pull-only device interface
(`include/device.h`, backend `src/device_alsa.c`). The main loop polls the
device's poll fd; on readiness it calls `device_read()` until it returns 0,
each call draining one period via `aud_process_capture_period()`:

1. `snd_pcm_avail_update()` — 0 means nothing to do.
2. `snd_pcm_readi()` of up to the scratch capacity frames into the device's
   internal buffer.
3. `device_read()` copies the period out to the caller's float array (in
   `loop.c` a static array of `INPUT_CALLBACK_SIZE` = 4800 samples).

Playback and TX remain on the plain `audio.h` API below.

## Playback path

Producers queue samples with `aud_output(buf)` — a copy into the ring; a
full ring silently truncates. The main loop drains the ring through
`aud_process_playback_period()` (up to one period per call: skip if
`ring_available()` is 0, else read and `snd_pcm_writei()`). The poll timeout
is short (`POLL_TIMEOUT_SHORT` = 10 ms) while playback is active, long
(`POLL_TIMEOUT_LONG` = 250 ms) when idle.

## Ring buffer

`src/ring.c` / `include/ring.h`: lock-free single-producer/single-consumer
circular buffer of float samples. SPSC safety comes from acquire/release
atomics on `read_idx`/`write_idx` alone — no mutex, no atomics on the data
itself. `ring_write`/`ring_read` handle wrap-around with two `memcpy` calls
and return the count actually transferred.

## Error recovery

Negative returns from `snd_pcm_readi`/`snd_pcm_avail_update`/
`snd_pcm_writei` go through `aud_stream_recover()`:

- **XRUN (`-EPIPE`)** — `snd_pcm_prepare()`, then `snd_pcm_start()` for capture.
- **Suspend (`-ESTRPIPE`)** — `snd_pcm_resume()` in a retry loop (100 ms),
  falling back to `snd_pcm_prepare()` if resume fails.

The failed operation is retried once after recovery.

## Constants

| Constant | Value | Meaning |
|---|---|---|
| `ALSA_PERIOD_SIZE` | 4096 | frames per ALSA period (~85 ms at 48 kHz) |
| `RING_BUFFER_SIZE` | 131072 | playback ring samples (~2.7 s at 48 kHz) |
| `INPUT_CALLBACK_SIZE` | 4800 | static capture buffer in `src/loop.c` |
| `POLL_TIMEOUT_SHORT/LONG` | 10 / 250 ms | poll timeout, playback active/idle |
