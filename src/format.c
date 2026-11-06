#include "format.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int audio_format_valid(const audio_format_t *f)
{
    if (f->type == 'F')
        return f->bits == 32 || f->bits == 64;
    if (f->type == 'S' || f->type == 'U')
        return f->bits == 8 || f->bits == 16 || f->bits == 32;
    return 0;
}

float audio_format_convert(const audio_format_t *f, const uint8_t *raw)
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

    int le = f->little_endian;
    if (f->type == 'F' && f->bits == 32)
    {
        memcpy(&val.f32, raw, 4);
        if (!le)
            byteswap32((uint32_t *)&val.f32);
        return val.f32;
    }
    if (f->type == 'F' && f->bits == 64)
    {
        memcpy(&val.f64, raw, 8);
        if (!le)
            byteswap64((uint64_t *)&val.f64);
        return (float)val.f64;
    }
    if (f->type == 'S' && f->bits == 8)
    {
        memcpy(&val.s8, raw, 1);
        return (float)val.s8 / 128.0f;
    }
    if (f->type == 'S' && f->bits == 16)
    {
        memcpy(&val.s16, raw, 2);
        if (!le)
            byteswap16((uint16_t *)&val.s16);
        return (float)val.s16 / 32768.0f;
    }
    if (f->type == 'S' && f->bits == 32)
    {
        memcpy(&val.s32, raw, 4);
        if (!le)
            byteswap32((uint32_t *)&val.s32);
        return (float)val.s32 / 2147483648.0f;
    }
    if (f->type == 'U' && f->bits == 8)
    {
        memcpy(&val.u8, raw, 1);
        return ((float)val.u8 / 128.0f) - 1.0f;
    }
    if (f->type == 'U' && f->bits == 16)
    {
        memcpy(&val.u16, raw, 2);
        if (!le)
            byteswap16(&val.u16);
        return ((float)val.u16 / 65536.0f) * 2.0f - 1.0f;
    }
    if (f->type == 'U' && f->bits == 32)
    {
        memcpy(&val.u32, raw, 4);
        if (!le)
            byteswap32(&val.u32);
        return ((float)val.u32 / 4294967296.0f) * 2.0f - 1.0f;
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

static int wav_parse_header(FILE *fp, audio_format_t *f)
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

            if (f->rate <= 0)
                f->rate = (int)rate;

            if (f->type == 0)
            {
                if (audio_format == 3) // IEEE float
                    f->type = 'F';
                else if (audio_format == 1) // PCM; 8-bit WAV is unsigned
                    f->type = bits == 8 ? 'U' : 'S';
                else
                    return -1;
                f->bits = bits;
                f->little_endian = 1;
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

int audio_format_probe(FILE *fp, audio_format_t *f)
{
    uint32_t riff_id = 0, wave_id = 0;
    int is_wav = fread(&riff_id, 4, 1, fp) == 1 && riff_id == 0x46464952 && // "RIFF"
                 fseek(fp, 4, SEEK_CUR) == 0 &&                             // riff size
                 fread(&wave_id, 4, 1, fp) == 1 && wave_id == 0x45564157;   // "WAVE"
    if (!is_wav)
    {
        if (fseek(fp, 0L, SEEK_SET))
            return -1;
        if (f->type == 0)
        {
            f->type = 'F';
            f->bits = 32;
        }
        if (f->little_endian < 0)
            f->little_endian = 1;
        if (f->rate <= 0)
            f->rate = 48000;
        return 0;
    }
    return wav_parse_header(fp, f);
}
