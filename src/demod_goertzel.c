#include "demod.h"
#include "common.h"
#include <math.h>

demod_grz_params_t grz_params_optim = {
    .window_size_mul = 1.08f,
    .agc_attack_ms = 0.01f,
    .ms_lpf_order = 2,
    .ms_lpf_cutoff_mul = 3.0f,
    .post_lpf_order = 6,
    .agc_release_ms = 83.4488f,
    .sym_clip = 0.4882f,
    .post_lpf_cutoff_mul = 0.8542f};

demod_grz_params_t grz_params_pesim = {
    .window_size_mul = 1.05f,
    .agc_attack_ms = 0.02f,
    .ms_lpf_order = 4,
    .ms_lpf_cutoff_mul = 3.0f,
    .post_lpf_order = 4,
    .agc_release_ms = 27.3333f,
    .sym_clip = 0.8939f,
    .post_lpf_cutoff_mul = 1.2000f};

void demod_grz_init(demod_grz_t *demod, demod_params_t *params, demod_grz_params_t *adv)
{
    nonnull(demod, "demod");
    nonnull(params, "params");
    nonnull(adv, "adv");

    int sample_window_size = (int)(0.5f + adv->window_size_mul * params->sample_rate / params->baud_rate);
    ring_simple_init(&demod->ring, sample_window_size);
    grz_init(&demod->mark_grz, sample_window_size, params->mark_freq, params->sample_rate);
    grz_init(&demod->space_grz, sample_window_size, params->space_freq, params->sample_rate);
    agc_init(&demod->mark_agc, adv->agc_attack_ms, adv->agc_release_ms, params->sample_rate);
    agc_init(&demod->space_agc, adv->agc_attack_ms, adv->agc_release_ms, params->sample_rate);
    bf_lpf_init(&demod->mark_lpf, adv->ms_lpf_order, adv->ms_lpf_cutoff_mul * params->baud_rate, params->sample_rate);
    bf_lpf_init(&demod->space_lpf, adv->ms_lpf_order, adv->ms_lpf_cutoff_mul * params->baud_rate, params->sample_rate);
    bf_lpf_init(&demod->post_filter, adv->post_lpf_order, adv->post_lpf_cutoff_mul * params->baud_rate, params->sample_rate);
    demod->sym_clip = adv->sym_clip;
}

float demod_grz_process(demod_grz_t *demod, float sample)
{
    nonnull(demod, "demod");

    float window_front = ring_simple_shift1(&demod->ring, sample);

    float mark_power = grz_process(&demod->mark_grz, sample, window_front);
    mark_power = agc_filter(&demod->mark_agc, mark_power);
    mark_power = bf_lpf_filter(&demod->mark_lpf, mark_power);

    float space_power = grz_process(&demod->space_grz, sample, window_front);
    space_power = agc_filter(&demod->space_agc, space_power);
    space_power = bf_lpf_filter(&demod->space_lpf, space_power);

    float symbol = mark_power - space_power;

    if (symbol > demod->sym_clip)
        symbol = demod->sym_clip;
    else if (symbol < -demod->sym_clip)
        symbol = -demod->sym_clip;
    symbol /= demod->sym_clip;

    symbol = bf_lpf_filter(&demod->post_filter, symbol);
    return symbol;
}

void demod_grz_free(demod_grz_t *demod)
{
    nonnull(demod, "demod");

    bf_lpf_free(&demod->mark_lpf);
    bf_lpf_free(&demod->space_lpf);
    bf_lpf_free(&demod->post_filter);
}
