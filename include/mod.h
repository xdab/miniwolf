#pragma once

#include "buffer.h"
#include "synth.h"

typedef struct modulator
{
    float mark_freq;
    float space_freq;
    float baud_rate;
    float sample_rate;
    synth_t fsk_synth;
} modulator_t;

void mod_init(modulator_t *mod, float mark_freq, float space_freq, float baud_rate, float sample_rate);

int mod_process(modulator_t *mod, int bit, float_buffer_t *out_samples_buf);

void mod_free(modulator_t *mod);
