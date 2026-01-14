#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "audio.h"
#include "fft.h"
#include "mavg.h"
#include "filter.h"
#include <argp.h>
#include <time.h>

#define INPUT_CALLBACK_SIZE 1024
#define EMA_TIME_CONSTANT 100
#define FREQ_COUNT 8

typedef struct cal_args
{
    char *dev_name;
    int rate;
    log_level_e log_level;
    float gain_2200;
} cal_args_t;

typedef struct bin
{
    float freq;
    ema_t avg_mag;
} bin_t;

static fft_t g_fft;
static bf_biquad_t g_hbf_filter;
static int g_sample_rate = 48000;
static int g_print_counter = 0;
static bin_t g_bins[FREQ_COUNT] = {
    {.freq = 700.0f},
    {.freq = 1200.0f},
    {.freq = 1700.0f},
    {.freq = 2200.0f},
    {.freq = 2700.0f},
    {.freq = 3200.0f},
    {.freq = 3700.0f},
    {.freq = 4200.0f},
};

static void print_spectrum(void)
{
    char output_buffer[512] = {0};
    size_t offset = 0;

    float ref_value = 0.0f;
    for (int i = 0; i < FREQ_COUNT; i++)
        if ((int)g_bins[i].freq == 1200)
        {
            ref_value = ema_get(&g_bins[i].avg_mag);
            break;
        }

    for (int i = 0; i < FREQ_COUNT; i++)
    {
        float value = ema_get(&g_bins[i].avg_mag);
        value -= ref_value;

        int written = snprintf(
            output_buffer + offset,
            sizeof(output_buffer) - offset,
            "%02d =%+5.1f%s",
            (int)(g_bins[i].freq / 100),
            value,
            (i < FREQ_COUNT - 1) ? " | " : " \n");

        if (written < 0 || (size_t)written >= sizeof(output_buffer) - offset)
        {
            output_buffer[sizeof(output_buffer) - 1] = '\0';
            break;
        }
        offset += written;
    }

    printf("%s", output_buffer);
}

static struct argp_option cal_options[] = {
    {"device", 'd', "DEVICE", 0, "Audio device name (required)", 1},
    {"rate", 'r', "RATE", 0, "Sample rate in Hz (default: 48000)", 1},
    {"eq2200", '2', "GAIN", 0, "Extra gain to apply at 2200Hz in dB (default: 0.0)", 2},
    {"verbose", 'v', 0, 0, "Enable verbose logs", 3},
    {"debug", 'V', 0, 0, "Enable verbose and debugging logs", 3},
    {0, 0, 0, 0, 0, 0}};

static error_t cal_parse_opt(int key, char *arg, struct argp_state *state)
{
    cal_args_t *args = state->input;
    switch (key)
    {
    case 'd':
        args->dev_name = arg;
        break;
    case 'r':
        args->rate = atoi(arg);
        break;
    case 'v':
        args->log_level = LOG_LEVEL_VERBOSE;
        break;
    case 'V':
        args->log_level = LOG_LEVEL_DEBUG;
        break;
    case '2':
        args->gain_2200 = atof(arg);
        break;
    case ARGP_KEY_NO_ARGS:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp cal_argp = {
    cal_options,
    cal_parse_opt,
    "",
    "Real-time spectrum analyzer and calibration utility for audio setup"};

static void cal_args_parse(int argc, char *argv[], cal_args_t *args)
{
    args->rate = 48000;
    args->log_level = LOG_LEVEL_STANDARD;
    args->dev_name = NULL;
    args->gain_2200 = 0.0f;

    argp_parse(&cal_argp, argc, argv, 0, 0, args);
}

static int audio_input_callback(float_buffer_t *buf)
{
    assert_buffer_valid(buf);

    // Apply high boost channel equalization
    for (int i = 0; i < buf->size; i++)
        buf->data[i] = bf_biquad_filter(&g_hbf_filter, buf->data[i]);

    fft_process(&g_fft, buf->data);

    float reference = buf->size / 2.0f;

    for (int i = 0; i < FREQ_COUNT; i++)
    {
        int bin = (int)(g_bins[i].freq * buf->size / g_sample_rate + 0.5f);
        float mag = fft_get_magnitude_db(&g_fft, bin, reference);
        if (isfinite(mag))
            ema_update(&g_bins[i].avg_mag, mag);
    }

    g_print_counter++;
    int chunks_per_second = g_sample_rate / INPUT_CALLBACK_SIZE;
    if (g_print_counter >= chunks_per_second)
    {
        g_print_counter = 0;
        print_spectrum();
    }

    return 0;
}

int main(int argc, char *argv[])
{
    cal_args_t args = {0};
    cal_args_parse(argc, argv, &args);
    _log_level = args.log_level;
    g_sample_rate = args.rate;

    fft_init(&g_fft, INPUT_CALLBACK_SIZE);
    bf_hbf_init(&g_hbf_filter, 4, 2200.0f, g_sample_rate, args.gain_2200);
    for (int i = 0; i < FREQ_COUNT; i++)
        ema_init(&g_bins[i].avg_mag, EMA_TIME_CONSTANT);

    if (aud_initialize())
        EXIT("Failed to initialize audio subsystem");

    if (!args.dev_name)
        EXIT("No input specified");

    if (args.rate <= 0)
        EXIT("Invalid sample rate specified");

    LOG("Using device '%s'", args.dev_name);

    if (aud_configure(args.dev_name, args.rate, 1, 1))
        EXIT("Failed to configure sound device");

    if (aud_start())
        EXIT("Failed to start audio streams");

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000L; // 100ms

    static float audio_input_buffer[INPUT_CALLBACK_SIZE];
    static float_buffer_t audio_buf = {
        .data = audio_input_buffer,
        .capacity = INPUT_CALLBACK_SIZE,
        .size = 0};

    for (;;)
    {
        aud_input(audio_input_callback, &audio_buf);
        nanosleep(&ts, NULL);
    }

NICE_EXIT:
    for (int i = 0; i < FREQ_COUNT; i++)
        ema_free(&g_bins[i].avg_mag);
    bf_biquad_free(&g_hbf_filter);
    fft_free(&g_fft);
    aud_terminate();
    return EXIT_SUCCESS;
}
