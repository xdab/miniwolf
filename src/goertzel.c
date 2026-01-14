#include "goertzel.h"
#include <math.h>
#include "common.h"

void grz_init(goertzel_t *grz, int window_size, float frequency, float sample_rate)
{
    nonnull(grz, "grz");
    nonzero(window_size, "window_size");

    grz->wsize = (float)window_size;
    int fft_bin = (int)(0.5f + ((grz->wsize * frequency) / sample_rate));
    float omega = (2.0f * M_PI * fft_bin) / grz->wsize;
    grz->coeff = 2.0f * cosf(omega);
    grz->q0 = grz->q1 = grz->q2 = 0.0f;
}

float grz_process(goertzel_t *grz, float x_n, float x_n_min_N)
{
    nonnull(grz, "grz");

    grz->q0 = x_n - x_n_min_N + grz->coeff * grz->q1 - grz->q2;
    grz->q2 = grz->q1;
    grz->q1 = grz->q0;
    float value = grz->q1 * grz->q1 + grz->q2 * grz->q2 - grz->q1 * grz->q2 * grz->coeff;
    value /= grz->wsize / 2.0f;
    return value;
}
