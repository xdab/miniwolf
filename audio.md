# audio.h/c - ALSA Audio Interface

## Overview

The audio module provides a real-time audio interface for miniwolf using ALSA (Advanced Linux Sound Architecture). It implements a poll-based, non-blocking architecture suitable for integration with event loops and socket selectors.

**Key Features:**
- Poll-based capture with file descriptor integration
- Ring-buffered playback output
- Automatic ALSA error recovery (xruns, suspends)
- Single-channel float32 audio processing
- Zero allocations in audio callbacks (real-time safe)

**File Size:** ~500 lines

---

## Architecture

### State Management

All state is maintained in static variables:
- `pcm_capture` / `pcm_playback`: ALSA PCM handles
- `output_ring`: Ring buffer for playback data (131072 samples)
- `capture_pfds[]`: Poll file descriptors for capture stream
- `period_size`: ALSA period size (4096 frames)

### Data Flow

**Capture (Input):**
```
ALSA capture → poll() ready → aud_process_capture_ready() → callback → user code
```

**Playback (Output):**
```
user code → aud_output() → ring buffer → aud_process_playback() → ALSA playback
```

---

## Public API

### Lifecycle

#### `int aud_initialize()`
Initialize audio subsystem. Allocates ring buffer for playback.

**Returns:** 0 on success, non-zero on failure

**Must be called before:** Any other audio functions

---

#### `void aud_terminate()`
Clean up audio subsystem. Closes ALSA streams and frees ring buffer.

**Safe to call:** Multiple times, even if not initialized

---

### Device Enumeration

#### `void aud_list_devices()`
Print available ALSA PCM devices to stdout.

**Output format:**
```
Cap  Name
IO   hw:0,0
I-   hw:1,0
-O   hw:2,0
```
- `I` = Input capable
- `O` = Output capable

---

### Configuration

#### `int aud_configure(const char *device_name, int sample_rate, bool do_input, bool do_output)`
Open and configure ALSA device(s).

**Parameters:**
- `device_name`: ALSA device name (e.g., "hw:0,0", "default")
- `sample_rate`: Sample rate in Hz (typically 48000)
- `do_input`: Enable capture stream
- `do_output`: Enable playback stream

**Returns:** 0 on success, -1 on failure

**Configuration applied:**
- Format: `SND_PCM_FORMAT_FLOAT` (32-bit float)
- Channels: 1 (mono)
- Access: Interleaved
- Period size: 4096 frames
- Buffer size: 4 × period size

**Notes:**
- At least one of `do_input`/`do_output` must be true
- Cleans up on partial failure (e.g., capture succeeds but playback fails)

---

#### `int aud_start()`
Prepare and start configured audio streams.

**Returns:** 0 on success, -1 on failure

**Actions:**
- Prepares ALSA streams
- Starts capture stream (playback starts on first write)
- Populates poll file descriptors for capture integration

**Must be called after:** `aud_configure()`

---

### Streaming Output (Playback)

#### `void aud_output(const float_buffer_t *buf)`
Queue audio samples for playback.

**Parameters:**
- `buf`: Buffer containing float samples to play

**Behavior:**
- Writes to internal ring buffer (non-blocking)
- If ring buffer full, oldest data is overwritten
- Actual playback occurs in `aud_process_playback()`

**Real-time safe:** Yes (no allocations, no blocking)

---

#### `void aud_process_playback()`
Process pending playback data.

**Behavior:**
- Reads from ring buffer
- Writes to ALSA playback stream
- Processes multiple periods if available
- Handles ALSA errors with automatic recovery

**Call frequency:** Regularly from main loop (e.g., after processing capture)

**Real-time safe:** Yes

---

### Poll-based Capture (Input)

#### `int aud_get_capture_fd_count(void)`
Get number of file descriptors for capture polling.

**Returns:** Number of FDs (typically 1, max 8)

**Usage:** Determine array size for poll integration

---

#### `int aud_get_capture_fd(int index)`
Get file descriptor for capture polling.

**Parameters:**
- `index`: FD index (0 to `aud_get_capture_fd_count() - 1`)

**Returns:** File descriptor, or -1 if index invalid

**Usage:** Add to `pollfd` array for `poll()` or socket selector

---

#### `int aud_process_capture_ready(int fd, input_callback_t *callback, float_buffer_t *buf)`
Process ready capture data.

**Parameters:**
- `fd`: File descriptor that became ready (from poll)
- `callback`: Function to call with captured audio
- `buf`: Buffer to receive audio data (capacity must be ≥ period size)

**Returns:** 0 on success, -1 on error

**Behavior:**
- Checks if FD corresponds to capture stream
- Reads available audio periods
- Calls `callback` for each period
- Handles ALSA errors with automatic recovery
- Processes multiple periods if available (prevents buffer overruns)

**Callback signature:**
```c
typedef int input_callback_t(float_buffer_t *buf);
```

**Real-time constraints:**
- Callback must not block
- Callback must not allocate memory
- Callback should process data quickly

---

## Internal Implementation

### ALSA Hardware Configuration

#### `aud_hw_params_apply()`
Apply hardware parameters to ALSA stream.

**Configuration:**
- Interleaved access
- Float32 format
- Mono (1 channel)
- Requested sample rate (nearest available)
- Period size: 4096 frames
- Buffer size: 16384 frames (4 periods)

---

### Error Recovery

#### `aud_stream_recover()`
Recover from ALSA stream errors.

**Handles:**
- **EPIPE (xrun):** Buffer underrun/overrun
  - Calls `snd_pcm_prepare()`
  - Restarts capture stream if needed
- **ESTRPIPE (suspend):** System suspend
  - Waits for resume with `snd_pcm_resume()`
  - Falls back to `snd_pcm_prepare()` if resume fails
  - Restarts capture stream if needed

**Logging:**
- Verbose: Recovery attempts
- Standard: Recovery failures

---

#### `aud_capture_restart()`
Restart capture stream after recovery.

**Why needed:** ALSA capture streams don't auto-restart after `snd_pcm_prepare()`

---

### Safe I/O

#### `aud_io_with_recovery()`
Generic I/O wrapper with automatic error recovery.

**Pattern:**
1. Attempt I/O operation
2. If error, attempt recovery
3. Retry I/O operation
4. Log if still failing

**Used by:**
- `aud_read_frames()`: Wraps `snd_pcm_readi()`
- `aud_write_frames()`: Wraps `snd_pcm_writei()`

---

### Capture Processing

#### `aud_capture_process_period()`
Process a single capture period.

**Returns:**
- `0`: Period processed successfully
- `1`: No data available (not an error)
- `-1`: Error occurred

**Logic:**
1. Check available frames with `snd_pcm_avail_update()`
2. If error, attempt recovery
3. If insufficient data (< period size), return 1
4. Read period with `aud_read_frames()`
5. Validate frame count
6. Call user callback

**Multiple periods:** `aud_process_capture_ready()` loops until no more data

---

### Playback Processing

#### `aud_playback_write_period()`
Write a single playback period.

**Returns:**
- `0`: Period written successfully
- `1`: No data available (not an error)
- `-1`: Error occurred

**Logic:**
1. Check ring buffer availability
2. Read up to one period from ring
3. Write to ALSA with `aud_write_frames()`

**Multiple periods:** `aud_process_playback()` loops until ring empty

---

## Usage Examples

### Basic Setup

```c
// Initialize
aud_initialize();

// Configure for 48kHz capture and playback
aud_configure("hw:0,0", 48000, true, true);

// Start streams
aud_start();
```

### Poll-based Capture Loop

```c
int audio_callback(float_buffer_t *buf) {
    // Process buf->data[0..buf->size-1]
    // Must be real-time safe!
    return 0;
}

int main() {
    // ... setup ...
    
    float_buffer_t audio_buf = {
        .data = malloc(4096 * sizeof(float)),
        .capacity = 4096,
        .size = 0
    };
    
    struct pollfd pfds[8];
    int nfds = aud_get_capture_fd_count();
    
    for (int i = 0; i < nfds; i++) {
        pfds[i].fd = aud_get_capture_fd(i);
        pfds[i].events = POLLIN;
    }
    
    while (running) {
        poll(pfds, nfds, 100);
        
        for (int i = 0; i < nfds; i++) {
            if (pfds[i].revents & POLLIN) {
                aud_process_capture_ready(pfds[i].fd, audio_callback, &audio_buf);
            }
        }
        
        aud_process_playback();
    }
    
    aud_terminate();
}
```

### Playback

```c
float_buffer_t samples = {
    .data = my_samples,
    .size = sample_count,
    .capacity = sample_count
};

aud_output(&samples);  // Queues for playback
aud_process_playback(); // Actually writes to ALSA
```

---

## Real-time Constraints

### Safe in Audio Callback
- ✅ Stack allocations
- ✅ Fixed-size buffers
- ✅ Arithmetic operations
- ✅ Calling other real-time safe functions

### Unsafe in Audio Callback
- ❌ `malloc()` / `free()`
- ❌ Locks (mutexes, semaphores)
- ❌ System calls (except audio I/O)
- ❌ File I/O
- ❌ Network I/O
- ❌ Logging (except debug macros)

### Module Compliance
- No heap allocations in `aud_process_capture_ready()`
- No heap allocations in `aud_output()`
- No heap allocations in `aud_process_playback()`
- All buffers pre-allocated or stack-based
- Lock-free ring buffer for playback

---

## Error Handling

### Return Codes
- `0`: Success
- `-1`: Error (logged)
- `1`: No data available (internal only)

### Logging Levels
- **Standard:** Critical errors, device open/close
- **Verbose:** Stream start, recovery attempts
- **Debug:** Poll events, multi-period processing

### Recovery Strategy
1. Detect error from ALSA call
2. Log error type (xrun, suspend, etc.)
3. Attempt automatic recovery
4. Retry original operation
5. If still failing, propagate error to caller

---

## Performance Characteristics

### Latency
- **Period size:** 4096 frames
- **At 48kHz:** ~85ms per period
- **Buffer size:** 4 periods = ~340ms total latency

### Throughput
- **Capture:** Processes multiple periods per poll event if available
- **Playback:** Writes multiple periods per call if ring buffer full
- **Typical:** 1 period per event under normal load

### Memory
- **Ring buffer:** 131072 × 4 bytes = 512 KB
- **Poll FDs:** 8 × sizeof(struct pollfd) ≈ 64 bytes
- **Stack per callback:** ~4096 × 4 bytes = 16 KB (output buffer)

---

## Integration Points

### With Socket Selector
```c
// Add audio FDs to selector
for (int i = 0; i < aud_get_capture_fd_count(); i++) {
    int fd = aud_get_capture_fd(i);
    socket_selector_add(&selector, fd);
}

// In event loop
if (socket_selector_is_ready(&selector, audio_fd)) {
    aud_process_capture_ready(audio_fd, callback, &buf);
}
```

### With Main Loop (loop.c)
- Capture FDs added to socket selector
- `aud_process_capture_ready()` called when FD ready
- `aud_process_playback()` called after capture processing
- Modulated samples queued with `aud_output()`

---

## Refactoring History

### Changes from Original
1. **Removed `aud_input()`** - Legacy blocking API, unused
2. **Consolidated I/O functions** - `safe_pcm_read/write` → `aud_io_with_recovery`
3. **Extracted restart logic** - `aud_capture_restart()` eliminates duplication
4. **Simplified error handling** - Reduced goto labels in `aud_configure()`
5. **Renamed functions** - More descriptive names:
   - `aud_pcm_setup` → `aud_hw_params_apply`
   - `aud_pcm_recover` → `aud_stream_recover`
   - `safe_pcm_io` → `aud_io_with_recovery`
   - `safe_pcm_read` → `aud_read_frames`
   - `safe_pcm_write` → `aud_write_frames`
   - `process_single_capture_period` → `aud_capture_process_period`
   - `process_single_playback_period` → `aud_playback_write_period`
6. **Reorganized structure** - Grouped by functionality with section headers

### Line Count Reduction
- **Before:** 530 lines
- **After:** ~450 lines
- **Reduction:** ~80 lines (15%)

---

## Dependencies

### External Libraries
- **ALSA:** `libasound` (snd_pcm_* functions)

### Internal Modules
- **ring.h:** Lock-free ring buffer for playback
- **common.h:** Logging macros, assertions
- **buffer.h:** `float_buffer_t` type definition

### System Headers
- `<poll.h>`: Poll file descriptor support
- `<stdio.h>`, `<stdlib.h>`, `<string.h>`: Standard C library

---

## Thread Safety

### Not Thread-Safe
All functions assume single-threaded access. No internal locking.

### Recommended Pattern
- Main thread: Calls all audio functions
- Audio callback: Runs in main thread context (poll-based, not ALSA callback)
- Ring buffer: Lock-free, but single reader/writer assumed

---

## Future Improvements

### Potential Enhancements
- [ ] Configurable period/buffer sizes
- [ ] Multi-channel support
- [ ] Sample rate conversion
- [ ] Latency measurement/reporting
- [ ] Hot-plug device support
- [ ] Separate capture/playback devices

### Not Planned
- ❌ ALSA callback mode (poll-based is intentional)
- ❌ Other audio backends (ALSA-only by design)
- ❌ Built-in resampling (use ALSA plugins)
