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
} sql_t;

void sql_init(sql_t *sql, float initial_threshold, float alpha, float sample_rate);

int sql_process(sql_t *sql, float sample);

float sql_envelope(sql_t *sql);

#endif
