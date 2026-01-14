#include "demod.h"
#include "common.h"
#include "filter.h"
#include "synth.h"
#include <math.h>
#include <stdlib.h>

demod_rrc_params_t rrc_params_default = {
    .rrc_rolloff = 0.2f,
    .rrc_width_sym = 2.8f,
    .agc_attack_ms = 0.0188f,
    .agc_release_ms = 251.9413f};

// Generate RRC impulse response
static float rrc_impulse(float t, float rolloff)
{
    float sinc, window;

    if (fabsf(t) < 0.001f)
        sinc = 1.0f;
    else
        sinc = sinf(M_PI * t) / (M_PI * t);

    float denom = 1.0f - (2.0f * rolloff * t) * (2.0f * rolloff * t);
    if (fabsf(denom) < 0.001f)
        window = M_PI / 4.0f;
    else
        window = cosf(M_PI * rolloff * t) / denom;

    return sinc * window;
}

// Generate RRC filter coefficients
static void generate_rrc_coeffs(float *coeffs, int taps, float rolloff, float samples_per_symbol)
{
    float center = (taps - 1.0f) / 2.0f;

    for (int k = 0; k < taps; k++)
    {
        float t = (k - center) / samples_per_symbol;
        coeffs[k] = rrc_impulse(t, rolloff);
    }

    // Normalize for unity gain
    float sum = 0.0f;
    for (int k = 0; k < taps; k++)
        sum += coeffs[k];
    for (int k = 0; k < taps; k++)
        coeffs[k] /= sum;
}

// FIR convolution using circular delay line
static float fir_convolve(const float *coeffs, float *delay_line, int taps, int delay_idx)
{
    float sum = 0.0f;

    // Perform convolution with circular delay line
    // delay_line[delay_idx] is the newest sample
    for (int i = 0; i < taps; i++)
    {
        int idx = (delay_idx - i + taps) % taps;
        sum += coeffs[i] * delay_line[idx];
    }

    return sum;
}

void demod_rrc_init(demod_rrc_t *demod, demod_params_t *params, demod_rrc_params_t *adv_params)
{
    nonnull(demod, "demod");
    nonnull(params, "params");
    nonnull(adv_params, "adv_params");

    // Calculate samples per symbol
    float samples_per_symbol = params->sample_rate / params->baud_rate;

    // Calculate filter length
    demod->filter_taps = (int)(adv_params->rrc_width_sym * samples_per_symbol) + 1;
    demod->rrc_rolloff = adv_params->rrc_rolloff;
    demod->rrc_width_sym = adv_params->rrc_width_sym;

    // Initialize quadrature synths
    synth_init(&demod->mark_sin_synth, params->sample_rate, params->mark_freq, 0.0f);
    synth_init(&demod->mark_cos_synth, params->sample_rate, params->mark_freq, M_PI / 2.0f);
    synth_init(&demod->space_sin_synth, params->sample_rate, params->space_freq, 0.0f);
    synth_init(&demod->space_cos_synth, params->sample_rate, params->space_freq, M_PI / 2.0f);

    // Allocate delay line
    demod->mark_i_delay = calloc(demod->filter_taps, sizeof(float));
    demod->mark_q_delay = calloc(demod->filter_taps, sizeof(float));
    demod->space_i_delay = calloc(demod->filter_taps, sizeof(float));
    demod->space_q_delay = calloc(demod->filter_taps, sizeof(float));
    demod->delay_idx = 0;

    // Allocate and generate RRC coefficients
    demod->mark_i_coeffs = malloc(sizeof(float) * demod->filter_taps);
    demod->mark_q_coeffs = malloc(sizeof(float) * demod->filter_taps);
    demod->space_i_coeffs = malloc(sizeof(float) * demod->filter_taps);
    demod->space_q_coeffs = malloc(sizeof(float) * demod->filter_taps);

    generate_rrc_coeffs(demod->mark_i_coeffs, demod->filter_taps, adv_params->rrc_rolloff, samples_per_symbol);
    generate_rrc_coeffs(demod->mark_q_coeffs, demod->filter_taps, adv_params->rrc_rolloff, samples_per_symbol);
    generate_rrc_coeffs(demod->space_i_coeffs, demod->filter_taps, adv_params->rrc_rolloff, samples_per_symbol);
    generate_rrc_coeffs(demod->space_q_coeffs, demod->filter_taps, adv_params->rrc_rolloff, samples_per_symbol);

    // Initialize AGC
    agc2_init(&demod->mark_agc, adv_params->agc_attack_ms, adv_params->agc_release_ms, params->sample_rate);
    agc2_init(&demod->space_agc, adv_params->agc_attack_ms, adv_params->agc_release_ms, params->sample_rate);
}

float demod_rrc_process(demod_rrc_t *demod, float sample)
{
    nonnull(demod, "demod");

    // Quadrature mixing using synths
    float mark_sin = synth_get_sample(&demod->mark_sin_synth);
    float mark_cos = synth_get_sample(&demod->mark_cos_synth);
    float mark_i = sample * mark_cos;
    float mark_q = sample * mark_sin;

    float space_sin = synth_get_sample(&demod->space_sin_synth);
    float space_cos = synth_get_sample(&demod->space_cos_synth);
    float space_i = sample * space_cos;
    float space_q = sample * space_sin;

    // Update delay lines (circular buffer)
    demod->mark_i_delay[demod->delay_idx] = mark_i;
    demod->mark_q_delay[demod->delay_idx] = mark_q;
    demod->space_i_delay[demod->delay_idx] = space_i;
    demod->space_q_delay[demod->delay_idx] = space_q;
    demod->delay_idx = (demod->delay_idx + 1) % demod->filter_taps;

    // RRC filtering using FIR convolution
    float mark_i_filt = fir_convolve(demod->mark_i_coeffs, demod->mark_i_delay, demod->filter_taps, demod->delay_idx);
    float mark_q_filt = fir_convolve(demod->mark_q_coeffs, demod->mark_q_delay, demod->filter_taps, demod->delay_idx);
    float space_i_filt = fir_convolve(demod->space_i_coeffs, demod->space_i_delay, demod->filter_taps, demod->delay_idx);
    float space_q_filt = fir_convolve(demod->space_q_coeffs, demod->space_q_delay, demod->filter_taps, demod->delay_idx);

    // Envelope detection
    float mark_amplitude = hypotf(mark_i_filt, mark_q_filt);
    float space_amplitude = hypotf(space_i_filt, space_q_filt);

    // AGC processing
    float mark_normalized = agc2_filter(&demod->mark_agc, mark_amplitude);
    float space_normalized = agc2_filter(&demod->space_agc, space_amplitude);

    // Final demodulation
    return mark_normalized - space_normalized;
}

void demod_rrc_free(demod_rrc_t *demod)
{
    nonnull(demod, "demod");

    free(demod->mark_i_delay);
    free(demod->mark_q_delay);
    free(demod->space_i_delay);
    free(demod->space_q_delay);

    free(demod->mark_i_coeffs);
    free(demod->mark_q_coeffs);
    free(demod->space_i_coeffs);
    free(demod->space_q_coeffs);
}
