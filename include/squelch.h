#ifndef SQUELCH_H
#define SQUELCH_H

#include "filter.h"
#include "agc.h"

typedef struct
{
    bf_lpf_t lpf;
    agc_t agc;
    float low_ema;
    float high_ema;
    float threshold;
    float alpha;
    float strength;
} sql_t;

typedef struct
{
    float sample_rate;
    float init_threshold;
    float strength;
} sql_params_t;

typedef struct
{
    int lpf_order;
    float lpf_cutoff_freq;
    float agc_ms;
    float tc_ms;
    float low_ema_est_mul;
    float high_ema_est_mul;
} sql_adv_params_t;

extern sql_adv_params_t sql_params_default;

void sql_init(sql_t *sql, sql_params_t *params, sql_adv_params_t *adv_params);

int sql_process(sql_t *sql, float sample);

float sql_envelope(sql_t *sql);

#endif
