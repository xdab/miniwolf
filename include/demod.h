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

typedef struct demod_quad_params
{
    int iq_lpf_order;
    float iq_lpf_cutoff_mul;
    int post_lpf_order;
    float post_lpf_cutoff_mul;
} demod_quad_params_t;

extern demod_quad_params_t quad_params_default;

typedef enum demod_type
{
    DEMOD_GOERTZEL_OPTIM = 1 << 0,
    DEMOD_GOERTZEL_PESIM = 1 << 1,
    DEMOD_QUADRATURE = 1 << 2,
    // Convenience values for multiple selections
    DEMOD_ALL_GOERTZEL = DEMOD_GOERTZEL_OPTIM | DEMOD_GOERTZEL_PESIM,
    DEMOD_ALL = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE,
} demod_type_t;

typedef union demod_union
{
    demod_grz_t grz;
    demod_quad_t quad;
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

void demod_quad_init(demod_quad_t *demod, demod_params_t *params, demod_quad_params_t *adv_params);

float demod_quad_process(demod_quad_t *demod, float sample);

void demod_quad_free(demod_quad_t *demod);

// "Abstract" functions

void demod_init(demod_t *demod, demod_type_t type, demod_params_t *params);

float demod_process(demod_t *demod, float sample);

void demod_free(demod_t *demod);

#endif
