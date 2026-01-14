#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fft.h"
#include "common.h"

void fft_init(fft_t *fft, int size)
{
    nonnull(fft, "fft");
    nonzero(size, "size");

    fft->size = size;
    fft->twiddle_real = malloc(sizeof(float) * size);
    fft->twiddle_imag = malloc(sizeof(float) * size);
    fft->work_real = malloc(sizeof(float) * size);
    fft->work_imag = malloc(sizeof(float) * size);

    if (!fft->twiddle_real || !fft->twiddle_imag || !fft->work_real || !fft->work_imag)
        EXIT("Failed to allocate FFT memory");

    for (int i = 0; i < size; i++)
    {
        float angle = -2.0f * M_PI * i / size;
        fft->twiddle_real[i] = cosf(angle);
        fft->twiddle_imag[i] = sinf(angle);
    }
}

static void bit_reverse_copy(const float *input, float *real, float *imag, int size)
{
    int bits = 0;
    for (int temp = size - 1; temp > 0; temp >>= 1)
        bits++;

    for (int i = 0; i < size; i++)
    {
        int reversed = 0;
        for (int j = 0; j < bits; j++)
            if (i & (1 << j))
                reversed |= 1 << (bits - 1 - j);

        real[reversed] = input[i];
        imag[reversed] = 0.0f;
    }
}

void fft_process(fft_t *fft, const float *input)
{
    nonnull(fft, "fft");
    nonnull(input, "input");

    bit_reverse_copy(input, fft->work_real, fft->work_imag, fft->size);

    for (int length = 2; length <= fft->size; length <<= 1)
    {
        int half_length = length / 2;
        float angle_step = -2.0f * M_PI / length;

        for (int i = 0; i < fft->size; i += length)
        {
            float angle = 0.0f;
            for (int j = 0; j < half_length; j++)
            {
                float cos_angle = cosf(angle);
                float sin_angle = sinf(angle);

                float temp_real = fft->work_real[i + j + half_length] * cos_angle -
                                  fft->work_imag[i + j + half_length] * sin_angle;
                float temp_imag = fft->work_real[i + j + half_length] * sin_angle +
                                  fft->work_imag[i + j + half_length] * cos_angle;

                fft->work_real[i + j + half_length] = fft->work_real[i + j] - temp_real;
                fft->work_imag[i + j + half_length] = fft->work_imag[i + j] - temp_imag;

                fft->work_real[i + j] += temp_real;
                fft->work_imag[i + j] += temp_imag;

                angle += angle_step;
            }
        }
    }
}

float fft_get_magnitude_db(fft_t *fft, int bin, float reference)
{
    nonnull(fft, "fft");

    if (bin < 0 || bin >= fft->size / 2)
        return -INFINITY;

    float real = fft->work_real[bin];
    float imag = fft->work_imag[bin];
    float magnitude = sqrtf(real * real + imag * imag);

    if (magnitude <= 0.0f || reference <= 0.0f)
        return -INFINITY;

    return 20.0f * log10f(magnitude / reference);
}

void fft_free(fft_t *fft)
{
    if (!fft)
        return;

    free(fft->twiddle_real);
    free(fft->twiddle_imag);
    free(fft->work_real);
    free(fft->work_imag);

    fft->twiddle_real = NULL;
    fft->twiddle_imag = NULL;
    fft->work_real = NULL;
    fft->work_imag = NULL;
}
