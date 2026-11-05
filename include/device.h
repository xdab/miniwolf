#pragma once

// Pull-only float-sample ingest. One interface, many backends (ALSA capture,
// raw/WAV file, later RTL I/Q): the receiver pulls blocks, the device never
// pushes a callback.

struct device;

typedef struct device_ops
{
    int (*read)(struct device *dev, float *buf, int max_samples);
    int (*poll_fd)(const struct device *dev);
    void (*free)(struct device *dev);
} device_ops_t;

typedef struct device
{
    const device_ops_t *ops;
} device_t;

// Read up to max_samples into buf. Returns the number of samples read
// (0 when the source has nothing available right now), or a negative value
// at end of stream / error.
static inline int device_read(device_t *dev, float *buf, int max_samples)
{
    return dev->ops->read(dev, buf, max_samples);
}

// Poll descriptor for integration with poll/epoll, or -1 when the device is
// not pollable (file sources just read to EOF).
static inline int device_poll_fd(const device_t *dev) { return dev->ops->poll_fd(dev); }

static inline void device_free(device_t *dev) { dev->ops->free(dev); }

// ALSA capture device. Expects aud_configure()/aud_start() to have set the
// capture stream up first (main.c owns that; playback/TX stays with audio.c).
device_t *device_open_alsa(void);

typedef struct
{
    char type;         // 'F'/'S'/'U' raw sample format; 0 = take from WAV header
    int bits;          // 8/16/32/64; 0 = take from WAV header
    int little_endian; // 1/0; -1 = take from WAV header (WAV is little-endian)
    int rate;          // in: 0 = take from WAV header; out: always the actual rate
} device_file_params_t;

// Raw PCM or WAV file source. WAV headers supply rate and (where left unset
// in params) the sample format; raw input defaults to F32 LE at 48 kHz.
device_t *device_open_file(const char *path, device_file_params_t *params);
