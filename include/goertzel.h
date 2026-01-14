#pragma once

typedef struct goertzel
{
    float wsize;
    float coeff;
    float q0, q1, q2;
} goertzel_t;

void grz_init(goertzel_t *grz, int window_size, float frequency, float sample_rate);

float grz_process(goertzel_t *grz, float x_n, float x_n_min_N);
