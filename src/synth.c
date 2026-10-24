#include "synth.h"
#include "buffer.h"
#include "common.h"
#include <math.h>

void synth_init(
    synth_t *synth,
    float sample_rate, // NOLINT(bugprone-easily-swappable-parameters)
    float frequency,
    float phase)
{
    nonnull(synth, "synth");
    nonzero(sample_rate, "sample_rate");
    nonzero(frequency, "frequency");

    synth->sample_rate = sample_rate;
    synth->frequency = frequency;
    synth->phase = phase;
}

void synth_get_samples(synth_t *synth, float_buffer_t *out_samples_buf)
{
    nonnull(synth, "synth");
    assert_buffer_valid(out_samples_buf);

    float omega = 2.0f * (float)M_PI * synth->frequency / synth->sample_rate;
    for (int i = 0; i < out_samples_buf->size; i++)
    {
        out_samples_buf->data[i] = sinf(omega + synth->phase);
        synth->phase += omega;
    }
    synth->phase = fmodf(synth->phase, 2.0f * (float)M_PI);
}

float synth_get_sample(synth_t *synth)
{
    nonnull(synth, "synth");

    float omega = 2.0f * (float)M_PI * synth->frequency / synth->sample_rate;
    float sample = sinf(synth->phase);
    synth->phase += omega;
    synth->phase = fmodf(synth->phase, 2.0f * (float)M_PI);
    return sample;
}
