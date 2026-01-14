#include "mod.h"
#include "common.h"
#include <stddef.h>
#include <math.h>

void mod_init(modulator_t *mod, float mark_freq, float space_freq, float baud_rate, float sample_rate)
{
    nonnull(mod, "mod");
    nonzero(mark_freq, "mark_freq");
    nonzero(space_freq, "space_freq");
    nonzero(baud_rate, "baud_rate");
    nonzero(sample_rate, "sample_rate");

    mod->mark_freq = mark_freq;
    mod->space_freq = space_freq;
    mod->baud_rate = baud_rate;
    mod->sample_rate = sample_rate;
    synth_init(&mod->fsk_synth, sample_rate, mark_freq, 0.0f);
}

int mod_process(modulator_t *mod, int bit, float_buffer_t *out_samples_buf)
{
    nonnull(mod, "mod");
    assert_buffer_valid(out_samples_buf);

    int num_samples = 1 + (int)(mod->sample_rate / mod->baud_rate);
    if (!fbuf_has_capacity_ge(out_samples_buf, num_samples))
        return -1;
    out_samples_buf->size = num_samples;

    mod->fsk_synth.frequency = (bit) ? mod->mark_freq : mod->space_freq;
    synth_get_samples(&mod->fsk_synth, out_samples_buf);
    return num_samples;
}

void mod_free(modulator_t *mod)
{
}
