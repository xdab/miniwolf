#include "squelch.h"
#include "common.h"
#include <math.h>

const float init_thr = 0.045;

sql_adv_params_t sql_params_default = {
    .lpf_order = 4,
    .lpf_cutoff_freq = 500.0f,
    .agc_ms = 40.0f,
    .tc_ms = 20.0e3f,
    .low_ema_est_mul = 0.25f,
    .high_ema_est_mul = 1.55f};

static float coefficient(float time_ms, float sample_rate)
{
    return 1.0f - expf(-1000.0f / (time_ms * sample_rate));
}

void sql_init(sql_t *sql, sql_params_t *params, sql_adv_params_t *adv_params)
{
    nonnull(sql, "sql");

    bf_lpf_init(&sql->lpf, adv_params->lpf_order, adv_params->lpf_cutoff_freq, params->sample_rate);
    agc_init(&sql->agc, adv_params->agc_ms, adv_params->agc_ms, params->sample_rate);
    sql->low_ema = params->init_threshold * adv_params->low_ema_est_mul;
    sql->high_ema = params->init_threshold * adv_params->high_ema_est_mul;
    sql->threshold = params->init_threshold;
    sql->alpha = coefficient(adv_params->tc_ms, params->sample_rate);
    sql->strength = params->strength;
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

    float eff_threshold = sql->low_ema * (1.0f - sql->strength) + sql->high_ema * sql->strength;
    return (envelope < eff_threshold) ? 1 : 0;
}

float sql_envelope(sql_t *sql)
{
    nonnull(sql, "sql");

    return sql->agc.envelope;
}
