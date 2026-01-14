#ifndef DEMOD_H
#define DEMOD_H

#include <stdint.h>

#include "ring.h"
#include "goertzel.h"
#include "agc.h"
#include "filter.h"
#include "synth.h"

typedef struct demod_goertzel
{
    ring_simple_t ring;
    goertzel_t mark_grz;
    goertzel_t space_grz;
    bf_lpf_t mark_lpf;
    bf_lpf_t space_lpf;
    agc_t mark_agc;
    agc_t space_agc;
    float sym_clip;
    bf_lpf_t post_filter;
} demod_grz_t;

typedef struct demod_goertzel_params
{
    float window_size_mul;
    float agc_attack_ms;
    float agc_release_ms;
    int ms_lpf_order;
    float ms_lpf_cutoff_mul;
    float sym_clip;
    int post_lpf_order;
    float post_lpf_cutoff_mul;
} demod_grz_params_t;

extern demod_grz_params_t grz_params_optim;
extern demod_grz_params_t grz_params_pesim;

typedef struct demod_quad
{
    float lo_i_prev;
    float lo_q_prev;
    float cos_inc;
    float sin_inc;
    float prev_phase;
    float scale;
    bf_lpf_t i_lpf;
    bf_lpf_t q_lpf;
    bf_lpf_t post_filter;
} demod_quad_t;

typedef struct demod_split_params
{
    int mark;
    float agc_attack_ms;
    float agc_release_ms;
    int ms_lpf_order;
    int post_lpf_order;
    float post_lpf_cutoff_mul;
} demod_split_params_t;

extern demod_split_params_t split_params_mark;
extern demod_split_params_t split_params_space;

typedef struct demod_split
{
    bf_spf_t branch_filter;
    bf_spf_filter_fn *branch_filter_fn;
    bf_lpf_t post_filter;
    agc2_t agc;
} demod_split_t;

typedef struct demod_rrc
{
    // DDS oscillators - quadrature synths
    synth_t mark_sin_synth;
    synth_t mark_cos_synth;
    synth_t space_sin_synth;
    synth_t space_cos_synth;

    // FIR delay lines (circular buffers)
    float *mark_i_delay;
    float *mark_q_delay;
    float *space_i_delay;
    float *space_q_delay;
    int delay_idx;

    // RRC filter coefficients (FIR)
    float *mark_i_coeffs;
    float *mark_q_coeffs;
    float *space_i_coeffs;
    float *space_q_coeffs;

    // AGC for envelope normalization
    agc2_t mark_agc;
    agc2_t space_agc;

    // Filter parameters
    int filter_taps;
    float rrc_rolloff;
    float rrc_width_sym;
} demod_rrc_t;

typedef struct demod_rrc_params
{
    float rrc_rolloff;
    float rrc_width_sym;
    float agc_attack_ms;
    float agc_release_ms;
} demod_rrc_params_t;

extern demod_rrc_params_t rrc_params_default;

typedef enum demod_type
{
    DEMOD_GOERTZEL_OPTIM = 1 << 0,
    DEMOD_GOERTZEL_PESIM = 1 << 1,
    DEMOD_QUADRATURE = 1 << 2,
    DEMOD_SPLIT_MARK = 1 << 3,
    DEMOD_SPLIT_SPACE = 1 << 4,
    DEMOD_RRC = 1 << 5, // (experimental, unsatisfactory performance)
    // Convenience values for multiple selections
    DEMOD_ALL_GOERTZEL = DEMOD_GOERTZEL_OPTIM | DEMOD_GOERTZEL_PESIM,
    DEMOD_ALL_SPLIT = DEMOD_SPLIT_MARK | DEMOD_SPLIT_SPACE,
    DEMOD_ALL = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE | DEMOD_ALL_SPLIT,
} demod_type_t;

typedef union demod_union
{
    demod_grz_t grz;
    demod_quad_t quad;
    demod_split_t split;
    demod_rrc_t rrc;
} demod_union_t;

typedef struct demod
{
    demod_union_t impl;
    demod_type_t type;
} demod_t;

typedef struct demod_params
{
    float mark_freq;
    float space_freq;
    float baud_rate;
    float sample_rate;
} demod_params_t;

// "Concrete" functions

void demod_grz_init(demod_grz_t *demod, demod_params_t *params, demod_grz_params_t *adv_params);

float demod_grz_process(demod_grz_t *demod, float sample);

void demod_grz_free(demod_grz_t *demod);

void demod_quad_init(demod_quad_t *demod, demod_params_t *params);

float demod_quad_process(demod_quad_t *demod, float sample);

void demod_quad_free(demod_quad_t *demod);

void demod_split_init(demod_split_t *demod, demod_params_t *params, demod_split_params_t *adv_params);

float demod_split_process(demod_split_t *demod, float sample);

void demod_split_free(demod_split_t *demod);

void demod_rrc_init(demod_rrc_t *demod, demod_params_t *params, demod_rrc_params_t *adv_params);

float demod_rrc_process(demod_rrc_t *demod, float sample);

void demod_rrc_free(demod_rrc_t *demod);

// "Abstract" functions

void demod_init(demod_t *demod, demod_type_t type, demod_params_t *params);

float demod_process(demod_t *demod, float sample);

void demod_free(demod_t *demod);

#endif
