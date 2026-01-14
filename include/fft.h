#pragma once

typedef struct fft
{
    int size;
    float *twiddle_real;
    float *twiddle_imag;
    float *work_real;
    float *work_imag;
} fft_t;

void fft_init(fft_t *fft, int size);

void fft_process(fft_t *fft, const float *input);

float fft_get_magnitude_db(fft_t *fft, int bin, float reference);

void fft_free(fft_t *fft);
