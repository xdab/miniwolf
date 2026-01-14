#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mavg.h"
#include "common.h"

void mavg_init(mavg_t *ma, int window_size)
{
    nonnull(ma, "ma");
    nonzero(window_size, "window_size");

    ma->values = malloc(sizeof(float) * window_size);
    if (!ma->values)
        EXIT("Failed to allocate moving average buffer");

    memset(ma->values, 0, sizeof(float) * window_size);
    ma->capacity = window_size;
    ma->count = 0;
    ma->index = 0;
    ma->sum = 0.0f;
}

float mavg_update(mavg_t *ma, float value)
{
    nonnull(ma, "ma");

    if (ma->count < ma->capacity)
    {
        ma->values[ma->index] = value;
        ma->sum += value;
        ma->count++;
    }
    else
    {
        ma->sum -= ma->values[ma->index];
        ma->values[ma->index] = value;
        ma->sum += value;
    }

    ma->index = (ma->index + 1) % ma->capacity;

    return ma->sum / ma->count;
}

float mavg_get(mavg_t *ma)
{
    nonnull(ma, "ma");

    if (ma->count == 0)
        return 0.0f;

    return ma->sum / ma->count;
}

void mavg_free(mavg_t *ma)
{
    if (!ma)
        return;

    free(ma->values);
    ma->values = NULL;
    ma->capacity = 0;
    ma->count = 0;
    ma->index = 0;
    ma->sum = 0.0f;
}

void ema_init(ema_t *ema, int time_constant_samples)
{
    nonnull(ema, "ema");
    nonzero(time_constant_samples, "time_constant_samples");

    ema->alpha = 1.0f - expf(-1.0f / time_constant_samples);
    ema->current_value = NAN;
}

float ema_update(ema_t *ema, float value)
{
    nonnull(ema, "ema");

    if (isnan(ema->current_value))
        ema->current_value = value;
    else
        ema->current_value = ema->alpha * value + (1.0f - ema->alpha) * ema->current_value;

    return ema->current_value;
}

float ema_get(ema_t *ema)
{
    nonnull(ema, "ema");

    if (isnan(ema->current_value))
        return 0.0f;

    return ema->current_value;
}

void ema_free(ema_t *ema)
{
    if (!ema)
        return;

    ema->current_value = NAN;
    ema->alpha = 0.0f;
}
