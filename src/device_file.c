#include "common.h"
#include "device.h"
#include "format.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    device_t base;
    FILE *fp;
    audio_format_t fmt;
    int bytes_per_sample;
    uint8_t *raw;
    int raw_capacity;
    int eof;
} device_file_t;

static int device_file_read(device_t *base, float *buf, int max_samples)
{
    device_file_t *dev = (device_file_t *)base;

    if (dev->eof)
        return -1;

    if (max_samples > dev->raw_capacity)
        max_samples = dev->raw_capacity;

    size_t n = fread(dev->raw, dev->bytes_per_sample, max_samples, dev->fp);
    if (n == 0)
    {
        dev->eof = 1;
        return -1;
    }

    for (size_t i = 0; i < n; i++)
        buf[i] = audio_format_convert(&dev->fmt, &dev->raw[i * dev->bytes_per_sample]);

    return (int)n;
}

static int device_file_poll_fd(const device_t *dev)
{
    (void)dev;
    return -1;
}

static void device_file_free(device_t *base)
{
    device_file_t *dev = (device_file_t *)base;
    if (dev->fp)
        (void)fclose(dev->fp);
    free(dev->raw);
    free(dev);
}

device_t *device_open_file(const char *path, audio_format_t *fmt)
{
    nonnull(path, "path");
    nonnull(fmt, "fmt");

    device_file_t *dev = calloc(1, sizeof(device_file_t));
    EXITIF(!dev, EXIT_FAILURE, "device_file allocation failed");

    static const device_ops_t ops = {
        .read = device_file_read, .poll_fd = device_file_poll_fd, .free = device_file_free};

    dev->fp = fopen(path, "rb");
    if (!dev->fp)
    {
        LOG("Error: Cannot open file '%s'.", path);
        free(dev);
        return NULL;
    }

    if (audio_format_probe(dev->fp, fmt))
    {
        LOG("Error: unsupported WAV file '%s' (need mono PCM/float).", path);
        goto FAIL;
    }
    if (!audio_format_valid(fmt))
    {
        LOG("Error: Invalid format. F: 32/64, S/U: 8/16/32.");
        goto FAIL;
    }

    dev->fmt = *fmt;
    dev->bytes_per_sample = fmt->bits / 8;
    // Sized for the largest block any caller asks for
    dev->raw_capacity = 8192 * dev->bytes_per_sample;
    dev->raw = malloc(dev->raw_capacity);
    if (!dev->raw)
        goto FAIL;

    dev->base.ops = &ops;
    return &dev->base;

FAIL:
    device_file_free(&dev->base);
    return NULL;
}
