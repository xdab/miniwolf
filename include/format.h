#pragma once

// Audio sample formats: raw sample conversion and WAV header probing.
// WAV input is mono PCM/float only; where fields are unset, WAV headers fill
// them, raw input defaults to F32 LE at 48 kHz.

#include <stdint.h>
#include <stdio.h>

typedef struct
{
    char type;         // 'F'/'S'/'U'; 0 = unset
    int bits;          // 8/16/32/64; 0 = unset
    int little_endian; // 1/0; -1 = unset
    int rate;          // 0 = unset
} audio_format_t;

// 0 when the combination is a supported format (F: 32/64, S/U: 8/16/32)
int audio_format_valid(const audio_format_t *f);

// Decodes one raw sample to native-endian float in [-1, 1)
float audio_format_convert(const audio_format_t *f, const uint8_t *raw);

// Sniffs the stream: WAV fills unset fields and leaves fp at the data chunk;
// raw input applies the defaults and rewinds. 0 ok, -1 error.
int audio_format_probe(FILE *fp, audio_format_t *f);
