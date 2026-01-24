#include "demod.h"
#include "common.h"
#include <math.h>

demod_quad_params_t quad_params_default = {
    .iq_lpf_order = 2,
    .iq_lpf_cutoff_mul = 0.5444f,
    .post_lpf_order = 4,
    .post_lpf_cutoff_mul = 0.5750f};

void demod_quad_init(demod_quad_t *demod, demod_params_t *params, demod_quad_params_t *adv_params)
{
    nonnull(demod, "demod");
    nonnull(params, "params");

    float center_freq = (params->mark_freq + params->space_freq) / 2.0f;
    float deviation = fabsf(params->mark_freq - params->space_freq) / 2.0f;
    float phase_inc = 2.0f * (float)M_PI * center_freq / params->sample_rate;
    demod->lo_i_prev = 1.0f;
    demod->lo_q_prev = 0.0f;
    demod->cos_inc = cosf(phase_inc);
    demod->sin_inc = sinf(phase_inc);
    demod->prev_phase = 0.0f;
    demod->scale = params->sample_rate / (2.0f * (float)M_PI * deviation);
    float iq_cutoff = adv_params->iq_lpf_cutoff_mul * fabsf(params->mark_freq - params->space_freq);
    bf_lpf_init(&demod->i_lpf, adv_params->iq_lpf_order, iq_cutoff, params->sample_rate);
    bf_lpf_init(&demod->q_lpf, adv_params->iq_lpf_order, iq_cutoff, params->sample_rate);
    float post_lpf_cutoff = adv_params->post_lpf_cutoff_mul * params->baud_rate;
    bf_lpf_init(&demod->post_filter, adv_params->post_lpf_order, post_lpf_cutoff, params->sample_rate);
}

float demod_quad_process(demod_quad_t *demod, float sample)
{
    nonnull(demod, "demod");

    // Mix input with I/Q references
    float i_in = sample * demod->lo_i_prev;
    float q_in = sample * demod->lo_q_prev;

    // Update oscillator recursively
    float next_i = demod->lo_i_prev * demod->cos_inc - demod->lo_q_prev * demod->sin_inc;
    float next_q = demod->lo_i_prev * demod->sin_inc + demod->lo_q_prev * demod->cos_inc;
    demod->lo_i_prev = next_i;
    demod->lo_q_prev = next_q;

    // Low-pass filter I/Q
    float i_filt = bf_lpf_filter(&demod->i_lpf, i_in);
    float q_filt = bf_lpf_filter(&demod->q_lpf, q_in);

    // Calculate phase
    float curr_phase = atan2f(q_filt, i_filt);
    float delta = curr_phase - demod->prev_phase;

    // Wrap phase difference to [-pi, pi]
    if (delta > (float)M_PI)
        delta -= 2.0f * (float)M_PI;
    if (delta < -(float)M_PI)
        delta += 2.0f * (float)M_PI;

    demod->prev_phase = curr_phase;

    float symbol = delta * demod->scale;

    symbol = bf_lpf_filter(&demod->post_filter, symbol);

    return symbol;
}

void demod_quad_free(demod_quad_t *demod)
{
    nonnull(demod, "demod");

    bf_lpf_free(&demod->i_lpf);
    bf_lpf_free(&demod->q_lpf);
    bf_lpf_free(&demod->post_filter);

#ifdef CONFIG_DEMOD_QUAD_DEBUG
    if (soft_raw_fp) {
        fclose(soft_raw_fp);
        soft_raw_fp = NULL;
    }
#endif
}
