#include "squelch.h"
#include "common.h"
#include <math.h>

static float coefficient(float time_ms, float sample_rate)
{
    return 1.0f - expf(-1000.0f / (time_ms * sample_rate));
}

void sql_init(sql_t *sql, float initial_threshold, float time_constant_ms, float sample_rate)
{
    nonnull(sql, "sql");

    bf_lpf_init(&sql->lpf, 8, 500.0f, sample_rate);
    agc_init(&sql->agc, 10.0f, 10.0f, sample_rate);
    sql->low_ema = initial_threshold * 0.25f; // Conservative low estimate
    sql->high_ema = initial_threshold * 1.5f; // Conservative high estimate
    sql->threshold = initial_threshold;
    sql->alpha = coefficient(time_constant_ms, sample_rate);
}

int sql_process(sql_t *sql, float sample)
{
    nonnull(sql, "sql");

    float filtered = bf_lpf_filter(&sql->lpf, sample);
    agc_filter(&sql->agc, filtered);
    float envelope = sql->agc.envelope;

    if (envelope <= 1e-3f)
        return 0;

    if (envelope < sql->threshold)
        sql->low_ema = sql->alpha * envelope + (1.0f - sql->alpha) * sql->low_ema;
    else
        sql->high_ema = sql->alpha * envelope + (1.0f - sql->alpha) * sql->high_ema;

    sql->threshold = (sql->low_ema + sql->high_ema) * 0.5f;
    sql->threshold *= 1.250f; // Slightly raise threshold to reduce packet loss

    return (envelope < sql->threshold) ? 1 : 0;
}

float sql_envelope(sql_t *sql)
{
    nonnull(sql, "sql");

    return sql->agc.envelope;
}
