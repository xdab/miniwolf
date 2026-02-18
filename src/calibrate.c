#include "calibrate.h"
#include "agc.h"
#include "audio.h"
#include "fft.h"
#include "filter.h"
#include "common.h"
#include <math.h>
#include <unistd.h>

#define INPUT_CALLBACK_SIZE 2048
#define WATERFALL_MAX_WIDTH 60

#define FREQ_MIN 100.0f
#define FREQ_MAX 3300.0f
#define DB_MIN -30.0f
#define DB_MAX 0.0f

static fft_t g_fft;
static bf_biquad_t g_hbf_filter;
static agc_t g_agc;
static int g_sample_rate = 0;
static int g_print_counter = 0;
static int g_bin_start = 0;
static int g_bin_count = 0;

static char magnitude_to_char(float db)
{
    if (db <= DB_MIN)
        return '0';
    if (db >= DB_MAX)
        return '9';

    float normalized = (db - DB_MIN) / (DB_MAX - DB_MIN);
    int level = (int)(normalized * 9.0f + 0.5f);
    if (level < 0)
        level = 0;
    if (level > 9)
        level = 9;

    return '0' + level;
}

static void print_waterfall(void)
{
    float reference = (INPUT_CALLBACK_SIZE / 2.0f) * g_agc.envelope;
    int output_width = (g_bin_count <= WATERFALL_MAX_WIDTH) ? g_bin_count : WATERFALL_MAX_WIDTH;

    for (int out = 0; out < output_width; out++)
    {
        float bin_start = (float)out * g_bin_count / output_width;
        float bin_end = (float)(out + 1) * g_bin_count / output_width;

        float sum = 0.0f;
        int count = 0;
        for (int bin = (int)bin_start; bin < (int)bin_end && bin < g_bin_count; bin++)
        {
            int actual_bin = g_bin_start + bin;
            sum += fft_get_magnitude_db(&g_fft, actual_bin, reference);
            count++;
        }

        float avg_db = (count > 0) ? sum / count : -INFINITY;
        putchar(magnitude_to_char(avg_db));
    }
}

static void print_numeric(void)
{
    float level = g_agc.envelope;
    float level_db = (level > 0.0f) ? 20.0f * log10f(level) : -INFINITY;

    int bin_1200 = (int)(1200.0f * INPUT_CALLBACK_SIZE / g_sample_rate + 0.5f);
    int bin_2200 = (int)(2200.0f * INPUT_CALLBACK_SIZE / g_sample_rate + 0.5f);
    float reference = (INPUT_CALLBACK_SIZE / 2.0f) * g_agc.envelope;

    float mag_1200 = fft_get_magnitude_db(&g_fft, bin_1200, reference);
    float mag_2200 = fft_get_magnitude_db(&g_fft, bin_2200, reference);
    float balance = mag_2200 - mag_1200;

    printf("  L:%.2f %+.1fdB  Bal:%+.1f\n", level, level_db, balance);
}

int calibrate_init(int sample_rate, float gain_2200)
{
    g_sample_rate = sample_rate;

    float bin_width = (float)g_sample_rate / INPUT_CALLBACK_SIZE;
    g_bin_start = (int)(FREQ_MIN / bin_width + 0.5f);
    g_bin_count = (int)((FREQ_MAX - FREQ_MIN) / bin_width + 0.5f);

    fft_init(&g_fft, INPUT_CALLBACK_SIZE);
    bf_hbf_init(&g_hbf_filter, 4, 2200.0f, g_sample_rate, gain_2200);
    agc_init(&g_agc, 2.5f, 250.0f, g_sample_rate);

    return 0;
}

int calibrate_audio_callback(float_buffer_t *buf)
{
    assert_buffer_valid(buf);

    for (int i = 0; i < buf->size; i++)
    {
        buf->data[i] = bf_biquad_filter(&g_hbf_filter, buf->data[i]);
        agc_filter(&g_agc, buf->data[i]);
    }

    fft_process(&g_fft, buf->data);

    print_waterfall();
    print_numeric();

    return 0;
}

void calibrate_run(void)
{
    static float audio_input_buffer[INPUT_CALLBACK_SIZE];
    static float_buffer_t audio_buf = {
        .data = audio_input_buffer,
        .capacity = INPUT_CALLBACK_SIZE,
        .size = 0};

    for (;;)
    {
        aud_process_capture(calibrate_audio_callback, &audio_buf);
        usleep(50000);
    }
}

void calibrate_free(void)
{
    bf_biquad_free(&g_hbf_filter);
    fft_free(&g_fft);
}