#pragma once

#include "buffer.h"

typedef struct synth
{
    float sample_rate;
    float frequency;
    float phase;
} synth_t;

void synth_init(synth_t *synth, float sample_rate, float frequency, float phase);

void synth_get_samples(synth_t *synth, float_buffer_t *out_samples_buf);

float synth_get_sample(synth_t *synth);
