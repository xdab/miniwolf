#include <math.h>
#include "demod.h"
#include "common.h"

demod_split_params_t split_params_mark = {
    .mark = 1,
    .agc_attack_ms = 0.01f,
    .agc_release_ms = 51.6680f,
    .ms_lpf_order = 4,
    .post_lpf_order = 6,
    .post_lpf_cutoff_mul = 0.8667f};

demod_split_params_t split_params_space = {
    .mark = 0,
    .agc_attack_ms = 0.01f,
    .agc_release_ms = 110.0f,
    .ms_lpf_order = 4,
    .post_lpf_order = 6,
    .post_lpf_cutoff_mul = 1.00f};

void demod_split_init(demod_split_t *demod, demod_params_t *params, demod_split_params_t *adv)
{
    nonnull(demod, "demod");
    nonnull(params, "params");
    nonnull(adv, "adv");

    float center_freq = 0.5f * (params->mark_freq + params->space_freq);
    if (adv->mark)
    {
        bf_lpf_init(&demod->branch_filter, adv->ms_lpf_order, center_freq, params->sample_rate);
        demod->branch_filter_fn = &bf_lpf_filter;
    }
    else
    {
        bf_hpf_init(&demod->branch_filter, adv->ms_lpf_order, center_freq, params->sample_rate);
        demod->branch_filter_fn = &bf_hpf_filter;
    }
    bf_lpf_init(&demod->post_filter, adv->post_lpf_order, adv->post_lpf_cutoff_mul * params->baud_rate, params->sample_rate);
    agc2_init(&demod->agc, adv->agc_attack_ms, adv->agc_release_ms, params->sample_rate);
}

float demod_split_process(demod_split_t *demod, float sample)
{
    nonnull(demod, "demod");

    float symbol = demod->branch_filter_fn(&demod->branch_filter, sample);
    symbol = fabsf(symbol);
    symbol = bf_lpf_filter(&demod->post_filter, symbol);
    symbol = agc2_filter(&demod->agc, symbol);
    return symbol;
}

void demod_split_free(demod_split_t *demod)
{
    nonnull(demod, "demod");
}