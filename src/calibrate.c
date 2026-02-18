#include "calibrate.h"
#include "agc.h"
#include "audio.h"
#include "fft.h"
#include "filter.h"
#include "ring.h"
#include "poller.h"
#include "common.h"
#include <math.h>
#include <unistd.h>
#include <errno.h>

#define INPUT_CALLBACK_SIZE 8192
#define RING_BUFFER_SIZE 16384
#define WATERFALL_MAX_WIDTH 55

#define FREQ_MIN 100.0f
#define FREQ_MAX 4100.0f
#define DB_MIN -36.0f
#define DB_MAX -12.0f

static fft_t g_fft;
static bf_biquad_t g_hbf_filter;
static agc_t g_agc;
static int g_sample_rate = 0;
static int g_bin_start = 0;
static int g_bin_count = 0;

static ring_buffer_t *g_ring = NULL;
static socket_poller_t g_poller;
static int g_audio_fd = -1;

static char magnitude_to_char(float db)
{
    if (db <= DB_MIN)
        return ' ';
    if (db >= DB_MAX)
        return '^';

    float normalized = (db - DB_MIN) / (DB_MAX - DB_MIN);
    int level = (int)(normalized * 9.0f + 0.5f);
    if (level < 0)
        level = 0;
    if (level > 9)
        level = 9;

    return " .:345678#"[level];
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
    float level_db = (level > 0.0f) ? 10.0f * log10f(level) : -INFINITY;

    int bin_1200 = (int)(1200.0f * INPUT_CALLBACK_SIZE / g_sample_rate + 0.5f);
    int bin_2200 = (int)(2200.0f * INPUT_CALLBACK_SIZE / g_sample_rate + 0.5f);
    float reference = (INPUT_CALLBACK_SIZE / 2.0f) * g_agc.envelope;

    float mag_1200 = fft_get_magnitude_db(&g_fft, bin_1200, reference);
    float mag_2200 = fft_get_magnitude_db(&g_fft, bin_2200, reference);
    float balance = mag_2200 - mag_1200;

    printf(" L:%.2f %+.1fdB  B:%+.1f\n", level, level_db, balance);
}

static void process_fft(float *samples, int size)
{
    for (int i = 0; i < size; i++)
    {
        samples[i] = bf_biquad_filter(&g_hbf_filter, samples[i]);
        agc_filter(&g_agc, samples[i]);
    }

    fft_process(&g_fft, samples);

    print_waterfall();
    print_numeric();
}

static int accumulate_audio_callback(float_buffer_t *buf)
{
    assert_buffer_valid(buf);
    ring_write(g_ring, buf->data, buf->size);
    return 0;
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

    ring_error_t ring_err = ring_init(&g_ring, RING_BUFFER_SIZE);
    if (ring_err != RING_SUCCESS)
    {
        LOG("failed to initialize ring buffer: %d", ring_err);
        return -1;
    }

    socket_poller_init(&g_poller);

    g_audio_fd = aud_get_poll_fd();
    if (g_audio_fd < 0)
    {
        LOG("failed to get audio poll descriptor");
        return -1;
    }

    if (socket_poller_add(&g_poller, g_audio_fd, POLLER_EV_IN) < 0)
    {
        LOG("failed to add audio fd to poller");
        return -1;
    }

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
    static float fft_buffer[INPUT_CALLBACK_SIZE];
    static float_buffer_t audio_buf = {
        .data = audio_input_buffer,
        .capacity = INPUT_CALLBACK_SIZE,
        .size = 0};

    for (;;)
    {
        int poll_ret = socket_poller_wait(&g_poller, -1);
        if (poll_ret < 0)
        {
            if (errno == EINTR)
                continue;
            LOG("poller wait error");
            break;
        }

        if (!socket_poller_is_ready(&g_poller, g_audio_fd))
            continue;

        aud_process_capture(accumulate_audio_callback, &audio_buf);

        while (ring_available(g_ring) >= INPUT_CALLBACK_SIZE)
        {
            size_t read = ring_read(g_ring, fft_buffer, INPUT_CALLBACK_SIZE);
            if (read == INPUT_CALLBACK_SIZE)
                process_fft(fft_buffer, INPUT_CALLBACK_SIZE);
        }
    }
}

void calibrate_free(void)
{
    socket_poller_free(&g_poller);

    if (g_ring)
    {
        ring_destroy(g_ring);
        g_ring = NULL;
    }

    bf_biquad_free(&g_hbf_filter);
    fft_free(&g_fft);
}