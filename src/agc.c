#include "agc.h"
#include "common.h"
#include <math.h>

#define MIN_ENVELOPE_VALUE 0.001f

static float coefficient(float time_ms, float sample_rate)
{
    return 1.0f - expf(-1000.0f / (time_ms * sample_rate));
}

void agc_init(agc_t *agc, float attack_ms, float release_ms, float sample_rate)
{
    nonnull(agc, "agc");

    agc->attack = coefficient(attack_ms, sample_rate);
    agc->release = coefficient(release_ms, sample_rate);
    agc->envelope = 1.0f;
}

float agc_filter(agc_t *agc, float sample)
{
    nonnull(agc, "agc");

    float abs_sample = fabsf(sample);

    if (abs_sample > agc->envelope)
        agc->envelope += agc->attack * (abs_sample - agc->envelope);
    else
        agc->envelope += agc->release * (abs_sample - agc->envelope);

    if (agc->envelope < MIN_ENVELOPE_VALUE)
        agc->envelope = MIN_ENVELOPE_VALUE;

    return sample / agc->envelope; // Normalize to [0, 1]
}

void agc2_init(agc2_t *agc, float attack_ms, float release_ms, float sample_rate)
{
    agc->attack = coefficient(attack_ms, sample_rate);
    agc->release = coefficient(release_ms, sample_rate);
    agc->lower = -0.0f;
    agc->upper = 0.0f;
}

float agc2_filter(agc2_t *agc, float sample)
{
    if (sample > agc->upper)
        agc->upper += agc->attack * (sample - agc->upper);
    else
        agc->upper += agc->release * (sample - agc->upper);

    if (sample < agc->lower)
        agc->lower += agc->attack * (sample - agc->lower);
    else
        agc->lower += agc->release * (sample - agc->lower);

    float envelope = agc->upper - agc->lower;
    if (envelope < MIN_ENVELOPE_VALUE)
        envelope = MIN_ENVELOPE_VALUE;

    return 2.0f * (sample - agc->lower) / envelope - 1.0f; // Normalize to [-1, 1]
}
