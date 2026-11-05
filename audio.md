# Audio Subsystem

This document describes how the application captures audio from ALSA, routes it through the system, and plays audio back to ALSA. It covers initialization, the polling loop, the capture path, the playback ring buffer, and error recovery — without reference to any protocol-specific processing.

---

## 1. Overview

The audio subsystem is defined in `src/audio.c` and `include/audio.h`. It provides:

- Capture of mono, 32-bit `float`, interleaved PCM samples from an ALSA device.
- Playback of mono, 32-bit `float` samples to the same or a different ALSA device.
- A lock-free single-producer/single-consumer ring buffer (`src/ring.c`) for decoupling the playback data source from the ALSA write path.
- A poll file descriptor that integrates with the application's main event loop so that audio readiness is detected via `poll()` (or `epoll`/`select`).

Helper types are defined in `libs/libtnc/include/buffer.h`:

```c
typedef struct float_buffer {
    float *data;
    int capacity;
    int size;
} float_buffer_t;
```

This struct carries every block of audio samples through the system.

---

## 2. Lifecycle and Initialization

The lifecycle has four phases, called in order from `main()`:

### 2.1 `aud_initialize()`

Allocates the **output ring buffer** — a lock-free circular buffer of `float` samples used only for playback (see §6). The ring is sized to `RING_BUFFER_SIZE` = 131072 samples (~2.7 seconds at 48 kHz).

```c
int aud_initialize(void)
{
    ring_error_t err = ring_init(&g_output_ring, RING_BUFFER_SIZE);
    ...
}
```

### 2.2 `aud_configure()`

Opens the ALSA device and applies hardware parameters. Called with a device name string (e.g. `"default"`), a sample rate, and two booleans `do_input` / `do_output` that control whether capture and/or playback streams are opened.

For each direction that is enabled:

1. **Open** the PCM device with `snd_pcm_open()`, requesting `SND_PCM_STREAM_CAPTURE` or `SND_PCM_STREAM_PLAYBACK`.
2. **Apply hardware parameters** via `aud_hw_params_apply()`:
   - Access: `SND_PCM_ACCESS_RW_INTERLEAVED`
   - Format: `SND_PCM_FORMAT_FLOAT` (native 32-bit `float`)
   - Channels: 1 (mono)
   - Rate: set to *near* the requested rate via `snd_pcm_hw_params_set_rate_near()`
   - Period size: `ALSA_PERIOD_SIZE` = 4096 frames
   - Buffer size: 4 × period size = 16384 frames

Both streams (if enabled) share the same device name and sample rate.

### 2.3 `aud_get_poll_fd()` (called after `aud_configure`)

Retrieves the native file descriptor for the capture PCM handle via `snd_pcm_poll_descriptors()`. This fd is registered with the application's poller so the main loop can wait on audio readiness alongside network and stdin fds.

```c
int aud_get_poll_fd(void)
{
    struct pollfd pfd;
    int count = snd_pcm_poll_descriptors(g_pcm_capture, &pfd, 1);
    return (count > 0) ? pfd.fd : -1;
}
```

### 2.4 `aud_start()`

Prepares and starts the capture stream with `snd_pcm_prepare()` + `snd_pcm_start()`. The playback stream is only prepared (not started) — data is pushed to it on-demand later.

---

## 3. Capture Path

### 3.1 Poll-driven triggering

The main loop waits on a set of file descriptors via `poll()`. When the capture fd signals readiness (`POLLIN`), the loop calls:

```c
aud_process_capture(audio_input_callback, &audio_buf);
```

The audio buffer used in the loop is a static `float_buffer_t` with a fixed capacity of 4800 samples (`INPUT_CALLBACK_SIZE`).

### 3.2 `aud_process_capture()`

This function loops, draining all available ALSA periods in a tight inner loop:

```c
int aud_process_capture(input_callback_t *callback, float_buffer_t *buf)
{
    int periods_processed = 0;
    while (aud_process_capture_period(callback, buf))
        periods_processed++;
    return periods_processed > 0 ? 0 : -1;
}
```

Each call to `aud_process_capture_period()` reads at most one period's worth of samples. This continues until no more frames are available from ALSA. The callback is invoked once per period read.

### 3.3 `aud_process_capture_period()` — single period read

1. **Check availability** — calls `snd_pcm_avail_update()` to get the number of frames ready in the hardware ring buffer. If 0, returns `false`.
2. **Read up to `buf->capacity` frames** via `snd_pcm_readi()` into `buf->data`. The actual number of frames read is stored in `buf->size`.
3. **Invoke the callback** `callback(buf)` — this is where the application processes the raw audio samples (filtering, analysis, etc.).
4. Returns `true` if at least one period was read, `false` otherwise.

### 3.4 Error recovery during capture

If `snd_pcm_readi()` or `snd_pcm_avail_update()` return a negative value, `aud_stream_recover()` is called:

- **XRUN (`-EPIPE`)**: The hardware ring buffer overran or underran. Calls `snd_pcm_prepare()` to reset the stream, then restarts capture with `snd_pcm_start()`.
- **Suspend (`-ESTRPIPE`)**: The device was suspended. Calls `snd_pcm_resume()` in a retry loop (100 ms sleep), falling back to `snd_pcm_prepare()` if resume fails.

After recovery, the operation is retried once.

---

## 4. Ring Buffer (Playback)

The ring buffer (`src/ring.c`, `include/ring.h`) is a **lock-free single-producer/single-consumer** circular buffer of `float` samples. It uses C11 atomic operations on `read_idx` and `write_idx` indices.

### 4.1 Internal structure

```c
struct ring_buffer {
    float *buffer;
    size_t size;
    _Atomic size_t read_idx;
    _Atomic size_t write_idx;
};
```

### 4.2 API

| Function | Description |
|---|---|
| `ring_init(ring, capacity)` | Allocates the struct and the sample buffer, zeroes indices. |
| `ring_destroy(ring)` | Frees both the buffer and the struct. |
| `ring_available(ring)` | Returns `write_idx - read_idx` — the number of readable samples. |
| `ring_write(ring, samples, n)` | Copies up to `n` samples into the ring, respecting available space. Handles wrap-around with two `memcpy` calls. Returns the number of samples actually written. |
| `ring_read(ring, samples, n)` | Copies up to `n` samples out of the ring, advancing `read_idx`. Handles wrap-around. Returns the number of samples actually read. |

### 4.3 Thread safety

The ring is safe for a single producer thread (`ring_write`) and a single consumer thread (`ring_read`) without any mutex. The atomic loads/stores on the indices provide acquire/release ordering sufficient for this SPSC pattern. The data buffer itself does not use atomics — the index ordering guarantees that the consumer never reads data the producer hasn't finished writing, and the producer never overwrites data the consumer hasn't finished reading.

---

## 5. Playback Path

### 5.1 Writing into the ring

Any part of the application can queue audio for playback by calling:

```c
void aud_output(const float_buffer_t *buf)
{
    ring_write(g_output_ring, buf->data, buf->size);
}
```

This copies samples from the provided buffer into the output ring. If the ring is full, `ring_write` silently truncates — only as many samples as there is free space are written.

### 5.2 Draining the ring to ALSA

Playback is driven from the main loop. At the top of every loop iteration, **before** waiting on poll:

```c
timeout_ms = aud_process_playback_period() ? POLL_TIMEOUT_SHORT : POLL_TIMEOUT_LONG;
```

`aud_process_playback_period()` reads one ALSA period (4096 frames) from the ring and writes it to the PCM playback device:

1. If `ring_available()` returns 0, return `false` immediately.
2. Read up to `ALSA_PERIOD_SIZE` samples from the ring.
3. Write them to ALSA via `snd_pcm_writei()`.

The `aud_process_playback()` function wraps this in a loop to drain multiple periods in one go. The main loop adjusts its poll timeout: when playback is active (data was written), it uses a short timeout (10 ms) so it can keep feeding the hardware; when idle, it uses a long timeout (250 ms) to save CPU.

Playback uses the same `aud_stream_recover()` error recovery as capture for XRUN and suspend events.

---

## 6. Putting It All Together — Main Loop Flow

1. **`aud_initialize()`** — allocates the output ring.
2. **`aud_configure()`** — opens ALSA capture and/or playback, configures HW params.
3. **`mini wolf_init()`** — calls `aud_get_poll_fd()` and registers the capture fd with the poller.
4. **`aud_start()`** — starts the capture stream.
5. **Main loop** (`loop_run`):
   - Attempt playback: if samples are queued in the ring, write up to one period to ALSA.
   - Set poll timeout: short (10 ms) if playback was active, long (250 ms) otherwise.
   - `poll()` on all fds (capture, stdin, TCP, UDP, UDS).
   - If capture fd is ready, call `aud_process_capture(callback, buf)` which drains all available periods, invoking the processing callback for each.
   - Repeat.

6. **`aud_terminate()`** — closes PCM handles, destroys the ring buffer.

---

## 7. Constants

| Constant | Value | Meaning |
|---|---|---|
| `ALSA_PERIOD_SIZE` | 4096 | Frames per ALSA period (~85 ms at 48 kHz) |
| `RING_BUFFER_SIZE` | 131072 | Playback ring capacity in samples (~2.7 s at 48 kHz) |
| `INPUT_CALLBACK_SIZE` | 4800 | Static capture buffer size in samples |

---

## 8. Summary for Reuse

To build a new audio-processing program on this foundation, you need:

- **ALSA capture**: open device → set HW params (float, mono, interleaved, period size) → get poll fd → `snd_pcm_readi()` in response to poll events.
- **Poll integration**: register the capture fd with your event loop; when it fires, read all available periods.
- **Data format**: samples arrive as `float_buffer_t` structs — a pointer to a `float` array, a capacity, and a filled size.
- **Ring buffer**: for any output path, use the lock-free SPSC ring. Write from the producer, read from the consumer (poll- or timer-driven), and write to ALSA via `snd_pcm_writei()`.
- **Error recovery**: handle `-EPIPE` (xrun → prepare + start) and `-ESTRPIPE` (suspend → resume + retry).