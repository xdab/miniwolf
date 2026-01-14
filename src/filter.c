#include "filter.h"
#include <stdlib.h>
#include <math.h>
#include "common.h"

void bf_lpf_init(bf_spf_t *filter, int order, float cutoff_freq, float sample_rate)
{
    nonnull(filter, "filter");
    nonzero(order, "order");

    filter->n = order / 2;
    filter->A = malloc(sizeof(float) * filter->n);
    filter->d1 = malloc(sizeof(float) * filter->n);
    filter->d2 = malloc(sizeof(float) * filter->n);
    filter->w0 = malloc(sizeof(float) * filter->n);
    filter->w1 = malloc(sizeof(float) * filter->n);
    filter->w2 = malloc(sizeof(float) * filter->n);

    float s = sample_rate;
    float f = cutoff_freq;
    float a = tanf(M_PI * f / s);
    float a2 = a * a;

    for (int i = 0; i < filter->n; i++)
    {
        float r = sinf(M_PI * (2.0f * i + 1.0f) / (4.0f * filter->n));
        s = a2 + 2.0f * r * a + 1.0f;
        filter->A[i] = a2 / s;
        filter->d1[i] = 2.0f * (1.0f - a2) / s;
        filter->d2[i] = -(a2 - 2.0f * r * a + 1.0f) / s;
        filter->w0[i] = 0.0f;
        filter->w1[i] = 0.0f;
        filter->w2[i] = 0.0f;
    }
}

float bf_lpf_filter(bf_spf_t *filter, float sample)
{
    nonnull(filter, "filter");

    for (int i = 0; i < filter->n; i++)
    {
        float temp_w0 = filter->d1[i] * filter->w1[i] + filter->d2[i] * filter->w2[i] + sample;
        filter->w0[i] = temp_w0;
        sample = filter->A[i] * (temp_w0 + 2.0f * filter->w1[i] + filter->w2[i]);
        filter->w2[i] = filter->w1[i];
        filter->w1[i] = temp_w0;
    }
    return sample;
}

void bf_lpf_free(bf_spf_t *filter)
{
    nonnull(filter, "filter");

    free(filter->A);
    free(filter->d1);
    free(filter->d2);
    free(filter->w0);
    free(filter->w1);
    free(filter->w2);
}

void bf_hpf_init(bf_spf_t *filter, int order, float cutoff_freq, float sample_rate)
{
    nonnull(filter, "filter");
    nonzero(order, "order");

    filter->n = order / 2;
    filter->A = malloc(sizeof(float) * filter->n);
    filter->d1 = malloc(sizeof(float) * filter->n);
    filter->d2 = malloc(sizeof(float) * filter->n);
    filter->w0 = malloc(sizeof(float) * filter->n);
    filter->w1 = malloc(sizeof(float) * filter->n);
    filter->w2 = malloc(sizeof(float) * filter->n);

    float s = sample_rate;
    float f = cutoff_freq;
    float a = tanf(M_PI * f / s);
    float a2 = a * a;

    for (int i = 0; i < filter->n; i++)
    {
        float r = sinf(M_PI * (2.0f * i + 1.0f) / (4.0f * filter->n));
        s = a2 + 2.0f * r * a + 1.0f;
        filter->A[i] = 1.0f / s;
        filter->d1[i] = 2.0f * (1.0f - a2) / s;
        filter->d2[i] = -(a2 - 2.0f * r * a + 1.0f) / s;
        filter->w0[i] = 0.0f;
        filter->w1[i] = 0.0f;
        filter->w2[i] = 0.0f;
    }
}

float bf_hpf_filter(bf_spf_t *filter, float sample)
{
    nonnull(filter, "filter");

    for (int i = 0; i < filter->n; i++)
    {
        float temp_w0 = filter->d1[i] * filter->w1[i] + filter->d2[i] * filter->w2[i] + sample;
        filter->w0[i] = temp_w0;
        sample = filter->A[i] * (temp_w0 - 2.0f * filter->w1[i] + filter->w2[i]);
        filter->w2[i] = filter->w1[i];
        filter->w1[i] = temp_w0;
    }
    return sample;
}

void bf_hpf_free(bf_spf_t *filter)
{
    nonnull(filter, "filter");

    free(filter->A);
    free(filter->d1);
    free(filter->d2);
    free(filter->w0);
    free(filter->w1);
    free(filter->w2);
}

void bf_bpf_init(bf_bpf_t *filter, int order, float low_cutoff_freq, float high_cutoff_freq, float sample_rate)
{
    nonnull(filter, "filter");
    nonzero(order, "order");

    filter->n = order / 4;
    filter->A = malloc(sizeof(float) * filter->n);
    filter->d1 = malloc(sizeof(float) * filter->n);
    filter->d2 = malloc(sizeof(float) * filter->n);
    filter->d3 = malloc(sizeof(float) * filter->n);
    filter->d4 = malloc(sizeof(float) * filter->n);
    filter->w0 = malloc(sizeof(float) * filter->n);
    filter->w1 = malloc(sizeof(float) * filter->n);
    filter->w2 = malloc(sizeof(float) * filter->n);
    filter->w3 = malloc(sizeof(float) * filter->n);
    filter->w4 = malloc(sizeof(float) * filter->n);

    float s = sample_rate;
    float fl = low_cutoff_freq;
    float fu = high_cutoff_freq;
    float a = cosf(M_PI * (fu + fl) / s) / cosf(M_PI * (fu - fl) / s);
    float b = tanf(M_PI * (fu - fl) / s);
    float a2 = a * a;
    float b2 = b * b;

    for (int i = 0; i < filter->n; i++)
    {
        float r = sinf(M_PI * (2.0f * i + 1.0f) / (4.0f * filter->n));
        s = b2 + 2.0f * r * b + 1.0f;
        filter->A[i] = b2 / s;
        filter->d1[i] = 4.0f * a * (1.0f + b * r) / s;
        filter->d2[i] = 2.0f * (b2 - 2.0f * a2 - 1.0f) / s;
        filter->d3[i] = 4.0f * a * (1.0f - b * r) / s;
        filter->d4[i] = -(b2 - 2.0f * r * b + 1.0f) / s;
        filter->w0[i] = 0.0f;
        filter->w1[i] = 0.0f;
        filter->w2[i] = 0.0f;
        filter->w3[i] = 0.0f;
        filter->w4[i] = 0.0f;
    }
}

float bf_bpf_filter(bf_bpf_t *filter, float sample)
{
    nonnull(filter, "filter");

    for (int i = 0; i < filter->n; i++)
    {
        float temp_w0 = filter->d1[i] * filter->w1[i] + filter->d2[i] * filter->w2[i] + filter->d3[i] * filter->w3[i] + filter->d4[i] * filter->w4[i] + sample;
        filter->w0[i] = temp_w0;
        sample = filter->A[i] * (temp_w0 - 2.0f * filter->w2[i] + filter->w4[i]);
        filter->w4[i] = filter->w3[i];
        filter->w3[i] = filter->w2[i];
        filter->w2[i] = filter->w1[i];
        filter->w1[i] = temp_w0;
    }
    return sample;
}

void bf_bpf_free(bf_bpf_t *filter)
{
    nonnull(filter, "filter");

    free(filter->A);
    free(filter->d1);
    free(filter->d2);
    free(filter->d3);
    free(filter->d4);
    free(filter->w0);
    free(filter->w1);
    free(filter->w2);
    free(filter->w3);
    free(filter->w4);
}

void bf_hbf_init(bf_biquad_t *filter, int order, float cutoff_freq, float sample_rate, float gain_db_max)
{
    nonnull(filter, "filter");
    nonzero(order, "order");

    filter->n = order / 2;
    filter->b0 = malloc(sizeof(float) * filter->n);
    filter->b1 = malloc(sizeof(float) * filter->n);
    filter->b2 = malloc(sizeof(float) * filter->n);
    filter->a1 = malloc(sizeof(float) * filter->n);
    filter->a2 = malloc(sizeof(float) * filter->n);
    filter->w1 = malloc(sizeof(float) * filter->n);
    filter->w2 = malloc(sizeof(float) * filter->n);

    float s = sample_rate;
    float f = cutoff_freq;
    float w0 = 2.0f * M_PI * f / s;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float A = powf(10.0f, gain_db_max / 40.0f); // shelf amplitude (max gain)
    float alpha = sinw0 / 2.0f * sqrtf(2.0f); // S = 1 (Butterworth)

    for (int i = 0; i < filter->n; i++)
    {
        // Standard high shelf biquad (Audio EQ Cookbook)
        float b0i = A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtf(A) * alpha);
        float b1i = -2 * A * ((A - 1) + (A + 1) * cosw0);
        float b2i = A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtf(A) * alpha);
        float a0i = (A + 1) - (A - 1) * cosw0 + 2 * sqrtf(A) * alpha;
        float a1i = 2 * ((A - 1) - (A + 1) * cosw0);
        float a2i = (A + 1) - (A - 1) * cosw0 - 2 * sqrtf(A) * alpha;

        // Normalize the coefficients
        filter->b0[i] = b0i / a0i;
        filter->b1[i] = b1i / a0i;
        filter->b2[i] = b2i / a0i;
        filter->a1[i] = a1i / a0i;
        filter->a2[i] = a2i / a0i;

        // Initialize state variables (w1 and w2 are the filter state variables for the biquad)
        filter->w1[i] = 0.0f;
        filter->w2[i] = 0.0f;
    }
}

float bf_biquad_filter(bf_biquad_t *filter, float sample)
{
    nonnull(filter, "filter");

    // Process through cascaded biquad stages (Direct Form II Transposed)
    for (int i = 0; i < filter->n; i++)
    {
        float w0 = sample - filter->a1[i] * filter->w1[i] - filter->a2[i] * filter->w2[i];
        sample = filter->b0[i] * w0 + filter->b1[i] * filter->w1[i] + filter->b2[i] * filter->w2[i];
        filter->w2[i] = filter->w1[i];
        filter->w1[i] = w0;
    }

    return sample;
}

void bf_biquad_free(bf_biquad_t *filter)
{
    nonnull(filter, "filter");

    free(filter->b0);
    free(filter->b1);
    free(filter->b2);
    free(filter->a1);
    free(filter->a2);
    free(filter->w1);
    free(filter->w2);

    filter->b0 = NULL;
    filter->b1 = NULL;
    filter->b2 = NULL;
    filter->a1 = NULL;
    filter->a2 = NULL;
    filter->w1 = NULL;
    filter->w2 = NULL;
}
