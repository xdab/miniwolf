#include "common.h"
#include "device.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    device_t base;
    FILE *fp;
    char type;
    int bits;
    int little_endian;
    int bytes_per_sample;
    uint8_t *raw;
    int raw_capacity;
    int eof;
} device_file_t;

static void byteswap16(uint16_t *v) { *v = (((*v & 0xFF) << 8) | ((*v >> 8) & 0xFF)); }

static void byteswap32(uint32_t *v)
{
    *v =
        (((*v & 0xFF) << 24) | (((*v >> 8) & 0xFF) << 16) | (((*v >> 16) & 0xFF) << 8) |
         ((*v >> 24) & 0xFF));
}

static void byteswap64(uint64_t *v)
{
    *v =
        (((*v & 0xFF) << 56) | (((*v >> 8) & 0xFF) << 48) | (((*v >> 16) & 0xFF) << 40) |
         (((*v >> 24) & 0xFF) << 32) | (((*v >> 32) & 0xFF) << 24) | (((*v >> 40) & 0xFF) << 16) |
         (((*v >> 48) & 0xFF) << 8) | ((*v >> 56) & 0xFF));
}

// type/bits/little_endian are format fields that always travel together
// type/bits/little_endian are format fields that always travel together
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static float convert_sample(const uint8_t *raw, char type, int bits, int little_endian)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    union
    {
        float f32;
        double f64;
        int8_t s8;
        int16_t s16;
        int32_t s32;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
    } val;

    if (type == 'F')
    {
        if (bits == 32)
        {
            memcpy(&val.f32, raw, 4);
            if (!little_endian)
                byteswap32((uint32_t *)&val.f32);
            return val.f32;
        }
        else if (bits == 64)
        {
            memcpy(&val.f64, raw, 8);
            if (!little_endian)
                byteswap64((uint64_t *)&val.f64);
            return (float)val.f64;
        }
    }
    else if (type == 'S')
    {
        if (bits == 8)
        {
            memcpy(&val.s8, raw, 1);
            return (float)val.s8 / 128.0f;
        }
        else if (bits == 16)
        {
            memcpy(&val.s16, raw, 2);
            if (!little_endian)
                byteswap16((uint16_t *)&val.s16);
            return (float)val.s16 / 32768.0f;
        }
        else if (bits == 32)
        {
            memcpy(&val.s32, raw, 4);
            if (!little_endian)
                byteswap32((uint32_t *)&val.s32);
            return (float)val.s32 / 2147483648.0f;
        }
    }
    else if (type == 'U')
    {
        if (bits == 8)
        {
            memcpy(&val.u8, raw, 1);
            return ((float)val.u8 / 128.0f) - 1.0f;
        }
        else if (bits == 16)
        {
            memcpy(&val.u16, raw, 2);
            if (!little_endian)
                byteswap16(&val.u16);
            return ((float)val.u16 / 65536.0f) * 2.0f - 1.0f;
        }
        else if (bits == 32)
        {
            memcpy(&val.u32, raw, 4);
            if (!little_endian)
                byteswap32(&val.u32);
            return ((float)val.u32 / 4294967296.0f) * 2.0f - 1.0f;
        }
    }

    return 0.0f;
}

static int read_u16le(FILE *fp, uint16_t *v) { return fread(v, 2, 1, fp) == 1 ? 0 : -1; }
static int read_u32le(FILE *fp, uint32_t *v) { return fread(v, 4, 1, fp) == 1 ? 0 : -1; }

static int chunk_skip(FILE *fp, uint32_t size)
{
    if (size & 1) // chunk payloads are word-aligned
        size++;
    return size > 0 && fseek(fp, (long)size, SEEK_CUR) ? -1 : 0;
}

// Parses the fmt chunk (filling rate and, where left unset, the sample
// format), then positions the stream at the start of the data chunk.
static int wav_parse_header(FILE *fp, device_file_params_t *params)
{
    uint32_t chunk_id, chunk_size;
    uint16_t audio_format = 0, channels = 0, bits = 0;
    int have_fmt = 0;

    while (read_u32le(fp, &chunk_id) == 0 && read_u32le(fp, &chunk_size) == 0)
    {
        if (chunk_id == 0x20746D66 && !have_fmt) // "fmt "
        {
            uint32_t rate;
            if (read_u16le(fp, &audio_format) || read_u16le(fp, &channels) ||
                read_u32le(fp, &rate) || fseek(fp, 6, SEEK_CUR) || // byte_rate + block_align
                read_u16le(fp, &bits))
                return -1;

            if (channels != 1)
                return -1;

            if (params->rate <= 0)
                params->rate = (int)rate;

            if (params->type == 0)
            {
                if (audio_format == 3) // IEEE float
                    params->type = 'F';
                else if (audio_format == 1) // PCM; 8-bit WAV is unsigned
                    params->type = bits == 8 ? 'U' : 'S';
                else
                    return -1;
                params->bits = bits;
                params->little_endian = 1;
            }

            have_fmt = 1;
            if (chunk_skip(fp, chunk_size - 16))
                return -1;
            continue;
        }

        if (chunk_id == 0x61746164 && have_fmt) // "data"
            return 0;

        if (chunk_skip(fp, chunk_size))
            return -1;
    }

    return -1;
}

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
        buf[i] = convert_sample(
            &dev->raw[i * dev->bytes_per_sample], dev->type, dev->bits, dev->little_endian);

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

device_t *device_open_file(const char *path, device_file_params_t *params)
{
    nonnull(path, "path");
    nonnull(params, "params");

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

    uint32_t riff_id = 0, wave_id = 0;
    int is_wav = fread(&riff_id, 4, 1, dev->fp) == 1 && riff_id == 0x46464952 && // "RIFF"
                 fseek(dev->fp, 4, SEEK_CUR) == 0 &&                             // riff size
                 fread(&wave_id, 4, 1, dev->fp) == 1 && wave_id == 0x45564157;   // "WAVE"
    if (!is_wav)
    {
        if (fseek(dev->fp, 0L, SEEK_SET))
        {
            LOG("Error: cannot rewind '%s'.", path);
            goto FAIL;
        }
        // Raw defaults when the header can't supply them: F32 LE, 48 kHz
        if (params->type == 0)
        {
            params->type = 'F';
            params->bits = 32;
        }
        if (params->little_endian < 0)
            params->little_endian = 1;
        if (params->rate <= 0)
            params->rate = 48000;
    }
    else if (wav_parse_header(dev->fp, params))
    {
        LOG("Error: unsupported WAV file '%s' (need mono PCM/float).", path);
        goto FAIL;
    }

    dev->type = params->type;
    dev->bits = params->bits;
    dev->little_endian = params->little_endian;
    dev->bytes_per_sample = params->bits / 8;
    params->rate = params->rate > 0 ? params->rate : 48000;

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
