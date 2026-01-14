#pragma once

typedef struct bf_spf
{
    int n;
    float *A;
    float *d1;
    float *d2;
    float *w0;
    float *w1;
    float *w2;
} bf_spf_t;

typedef bf_spf_t bf_lpf_t;
typedef bf_spf_t bf_hpf_t;
typedef float bf_spf_filter_fn(bf_spf_t *f, float s);

void bf_lpf_init(bf_spf_t *filter, int order, float cutoff_freq, float sample_rate);

float bf_lpf_filter(bf_spf_t *filter, float sample);

void bf_lpf_free(bf_spf_t *filter);

void bf_hpf_init(bf_spf_t *filter, int order, float cutoff_freq, float sample_rate);

float bf_hpf_filter(bf_spf_t *filter, float sample);

void bf_hpf_free(bf_spf_t *filter);

//

typedef struct bf_bpf
{
    int n;
    float *A;
    float *d1;
    float *d2;
    float *d3;
    float *d4;
    float *w0;
    float *w1;
    float *w2;
    float *w3;
    float *w4;
} bf_bpf_t;

void bf_bpf_init(bf_bpf_t *filter, int order, float low_cutoff_freq, float high_cutoff_freq, float sample_rate);

float bf_bpf_filter(bf_bpf_t *filter, float sample);

void bf_bpf_free(bf_bpf_t *filter);

//

typedef struct bf_biquad
{
    int n;
    float *b0;
    float *b1;
    float *b2;
    float *a1;
    float *a2;
    float *w1;
    float *w2;
} bf_biquad_t;

float bf_biquad_filter(bf_biquad_t *filter, float sample);

void bf_hbf_init(bf_biquad_t *filter, int order, float cutoff_freq, float sample_rate, float gain_db);

void bf_biquad_free(bf_biquad_t *filter);
