#include "audio.h"
#include "buffer.h"
#include "common.h"
#include "device.h"
#include <stdlib.h>
#include <string.h>

typedef struct
{
    device_t base;
    float_buffer_t scratch;
    float scratch_data[8192];
    // One drained ALSA period sits here between pulls
    int pending;
} device_alsa_t;

// ALSA pushes via callback; the pull interface hands out the parked period.
static int alsa_capture_cb(float_buffer_t *buf)
{
    assert_buffer_valid(buf);
    return 0;
}

static int device_alsa_read(device_t *base, float *buf, int max_samples)
{
    device_alsa_t *dev = (device_alsa_t *)base;

    if (max_samples <= 0)
        return 0;

    for (;;)
    {
        if (dev->pending > 0)
        {
            int n = dev->pending < max_samples ? dev->pending : max_samples;
            memcpy(buf, dev->scratch.data, n * sizeof(float));
            memmove(dev->scratch.data, dev->scratch.data + n, (dev->pending - n) * sizeof(float));
            dev->pending -= n;
            return n;
        }

        dev->scratch.size = 0;
        if (!aud_process_capture_period(alsa_capture_cb, &dev->scratch))
            return 0; // nothing available now
        dev->pending = dev->scratch.size;
    }
}

static int device_alsa_poll_fd(const device_t *dev)
{
    (void)dev;
    return aud_get_poll_fd();
}

static void device_alsa_free(device_t *base) { free(base); }

device_t *device_open_alsa(void)
{
    device_alsa_t *dev = calloc(1, sizeof(device_alsa_t));
    EXITIF(!dev, EXIT_FAILURE, "device_alsa allocation failed");

    static const device_ops_t ops = {
        .read = device_alsa_read, .poll_fd = device_alsa_poll_fd, .free = device_alsa_free};

    dev->scratch.data = dev->scratch_data;
    dev->scratch.capacity = sizeof(dev->scratch_data) / sizeof(float);
    dev->scratch.size = 0;
    dev->base.ops = &ops;
    return &dev->base;
}
