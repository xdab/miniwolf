#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <argp.h>
#include <time.h>
#include "demod.h"
#include "bitclk.h"
#include "hldc.h"
#include "filter.h"
#include "common.h"
#include "buffer.h"

typedef struct modem_simple
{
    demod_t demod;
    bitclk2_t bit_detector;
    hldc_deframer_t deframer;
} modem_simple_t;

const float opt_param_min = 45.0f;
const float opt_param_max = 60.0f;
const float opt_param_step = 1.667f;

static void modem_simple_init(modem_simple_t *m, float sample_rate, float p)
{
    nonnull(m, "m");

    demod_params_t params = {
        .mark_freq = 1200.0f,
        .space_freq = 2200.0f,
        .baud_rate = 1200.0f,
        .sample_rate = sample_rate};

    demod_split_params_t local_params = split_params_mark;
    local_params.agc_release_ms = p;
    demod_split_init(&m->demod.impl.split, &params, &local_params);
    m->demod.type = DEMOD_SPLIT_MARK;

    bitclk2_init(&m->bit_detector, sample_rate, 1200.0f);
    hldc_deframer_init(&m->deframer);
}

static int modem_simple_demodulate(modem_simple_t *m, const float_buffer_t *sample_buf, buffer_t *out_frame_buf)
{
    nonnull(m, "m");
    assert_buffer_valid(sample_buf);
    assert_buffer_valid(out_frame_buf);

    int ret = 0;
    uint16_t crc = 0;

    for (int i = 0; i < sample_buf->size; i++)
    {
        float sample = sample_buf->data[i];
        float symbol = demod_process(&m->demod, sample);
        int bit = bitclk2_detect(&m->bit_detector, symbol);

        if (bit != BITCLK_NONE)
        {
            hldc_error_e result = hldc_deframer_process(&m->deframer, bit, out_frame_buf, &crc);
            if (result < 0)
                LOGV("error %d while processing sample", result);
            if (out_frame_buf->size <= 0)
                continue;

            ret = out_frame_buf->size;
        }
    }

    return ret;
}

static void modem_simple_free(modem_simple_t *m)
{
    nonnull(m, "m");
    demod_free(&m->demod);
}

typedef struct optim_args
{
    char *input_file;
    int rate;
    log_level_e log_level;
    char type;
    int bits;
    int little_endian;
    float gain_2200;
} optim_args_t;

static float convert_sample(const uint8_t *raw, char type, int bits, int little_endian)
{
    union
    {
        float f32;
        double f64;
        int8_t s8;
        int16_t s16;
        int32_t s32;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
    } val;

    if (type == 'F')
    {
        if (bits == 32)
        {
            memcpy(&val.f32, raw, 4);
            if (!little_endian)
                byteswap32((uint32_t *)&val.f32);
            return val.f32;
        }
        else if (bits == 64)
        {
            memcpy(&val.f64, raw, 8);
            if (!little_endian)
                byteswap64((uint64_t *)&val.f64);
            return (float)val.f64;
        }
    }
    else if (type == 'S')
    {
        if (bits == 8)
        {
            memcpy(&val.s8, raw, 1);
            return val.s8 / 128.0f;
        }
        else if (bits == 16)
        {
            memcpy(&val.s16, raw, 2);
            if (!little_endian)
                byteswap16((uint16_t *)&val.s16);
            return val.s16 / 32768.0f;
        }
        else if (bits == 32)
        {
            memcpy(&val.s32, raw, 4);
            if (!little_endian)
                byteswap32((uint32_t *)&val.s32);
            return val.s32 / 2147483648.0f;
        }
    }
    else if (type == 'U')
    {
        if (bits == 8)
        {
            memcpy(&val.u8, raw, 1);
            return (val.u8 / 128.0f) - 1.0f;
        }
        else if (bits == 16)
        {
            memcpy(&val.u16, raw, 2);
            if (!little_endian)
                byteswap16(&val.u16);
            return (val.u16 / 65536.0f) * 2.0f - 1.0f;
        }
        else if (bits == 32)
        {
            memcpy(&val.u32, raw, 4);
            if (!little_endian)
                byteswap32(&val.u32);
            return (val.u32 / 4294967296.0f) * 2.0f - 1.0f;
        }
    }

    return 0.0f;
}

static struct argp_option optim_options[] = {
    {"file", 'f', "FILE", 0, "Input raw audio file (required)", 1},
    {"rate", 'r', "RATE", 0, "Sample rate in Hz (default: 48000)", 1},
    {"format", 'F', "FORMAT", 0, "Audio format: F32, F64, S8, S16, S32, U8, U16, U32 (default: F32)", 2},
    {"endian", 'e', "ENDIAN", 0, "Byte order: LE (little-endian, default) or BE (big-endian)", 2},
    {"eq2200", '2', "GAIN", 0, "Extra gain to apply at 2200Hz in dB (default: 0.0)", 2},
    {"verbose", 'v', 0, 0, "Enable verbose logs", 3},
    {"debug", 'V', 0, 0, "Enable verbose and debugging logs", 3},
    {0, 0, 0, 0, 0, 0}};

static error_t optim_parse_opt(int key, char *arg, struct argp_state *state)
{
    optim_args_t *args = state->input;
    switch (key)
    {
    case 'f':
        args->input_file = arg;
        break;
    case 'r':
        args->rate = atoi(arg);
        break;
    case 'F':
        if (strlen(arg) >= 2)
        {
            args->type = arg[0];
            args->bits = atoi(&arg[1]);
        }
        break;
    case 'e':
        if (strcmp(arg, "BE") == 0 || strcmp(arg, "be") == 0)
            args->little_endian = 0;
        else if (strcmp(arg, "LE") == 0 || strcmp(arg, "le") == 0)
            args->little_endian = 1;
        break;
    case '2':
        args->gain_2200 = atof(arg);
        break;
    case 'v':
        args->log_level = LOG_LEVEL_VERBOSE;
        break;
    case 'V':
        args->log_level = LOG_LEVEL_DEBUG;
        break;
    case ARGP_KEY_NO_ARGS:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp optim_argp = {
    optim_options,
    optim_parse_opt,
    "",
    "Miniwolf hyperparameter optimiziation tool"};

static void optim_args_parse(int argc, char *argv[], optim_args_t *args)
{
    args->rate = 48000;
    args->log_level = LOG_LEVEL_STANDARD;
    args->input_file = NULL;
    args->type = 'F';
    args->bits = 32;
    args->little_endian = 1;
    args->gain_2200 = 0.0f;

    argp_parse(&optim_argp, argc, argv, 0, 0, args);
}

static int evaluate_modem(modem_simple_t *modem, float_buffer_t *samples_buf, size_t offset, size_t size)
{
    const int proc_chunk_size = 512;
    uint8_t frame_buffer[512];
    int frame_count = 0;

    size_t end = offset + size;
    for (size_t i = offset; i < end; i += proc_chunk_size)
    {
        size_t proc_size = (i + proc_chunk_size <= end) ? proc_chunk_size : (end - i);
        float_buffer_t samples_sub_buf = {.data = samples_buf->data + i, .capacity = proc_size, .size = proc_size};
        buffer_t frame_buf = {.data = frame_buffer, .capacity = sizeof(frame_buffer), .size = 0};
        int frame_len = modem_simple_demodulate(modem, &samples_sub_buf, &frame_buf);
        if (frame_len > 0)
            frame_count++;
    }

    return frame_count;
}

int main(int argc, char *argv[])
{
    int exit_code = EXIT_SUCCESS;
    optim_args_t args = {0};
    optim_args_parse(argc, argv, &args);
    _log_level = args.log_level;

    if (!args.input_file)
    {
        LOG("Error: --file <input_file> required.");
        goto ERROR;
    }

    if (args.rate <= 0)
    {
        LOG("Error: Invalid sample rate.");
        goto ERROR;
    }

    if ((args.type == 'F' && (args.bits != 32 && args.bits != 64)) ||
        (args.type != 'F' && (args.bits != 8 && args.bits != 16 && args.bits != 32)))
    {
        LOG("Error: Invalid format. F: 32/64, S/U: 8/16/32.");
        goto ERROR;
    }

    float sample_rate = (float)args.rate;
    int bytes_per_sample = args.bits / 8;

    LOGV("Sample rate: %.0f Hz", sample_rate);
    LOGV("Format: %c%d %s", args.type, args.bits, args.little_endian ? "LE" : "BE");

    FILE *fp = fopen(args.input_file, "rb");
    if (!fp)
    {
        fprintf(stderr, "Error: Cannot open file '%s'.\n", args.input_file);
        goto ERROR;
    }

    fseek(fp, 0, SEEK_END);
    size_t file_len = ftell(fp);
    rewind(fp);
    size_t samples_len = file_len / bytes_per_sample;

    float *samples = (float *)malloc(samples_len * sizeof(float));
    nonnull(samples, "samples");
    float_buffer_t samples_buf = {.data = samples, .capacity = samples_len, .size = samples_len};

    bf_biquad_t g_hbf_filter;
    bf_hbf_init(&g_hbf_filter, 4, 2200.0f, sample_rate, args.gain_2200);

    for (size_t i = 0; i < samples_len; i++)
    {
        uint8_t raw_buffer[8];
        size_t read_count = fread(raw_buffer, bytes_per_sample, 1, fp);
        nonzero(read_count, "read_count");
        samples[i] = convert_sample(raw_buffer, args.type, args.bits, args.little_endian);
        samples[i] = bf_biquad_filter(&g_hbf_filter, samples[i]);
    }

    fclose(fp);
    bf_biquad_free(&g_hbf_filter);

    LOG("loaded %ld samples", samples_len);

    const int num_iterations = (int)((opt_param_max - opt_param_min) / opt_param_step) + 1;

    LOG("will run %d iteration", num_iterations);

    modem_simple_t *modems = (modem_simple_t *)malloc(num_iterations * sizeof(modem_simple_t));
    int *frame_counts = (int *)malloc(num_iterations * sizeof(int));
    float *param_values = (float *)malloc(num_iterations * sizeof(float));

    nonnull(modems, "modems");
    nonnull(frame_counts, "frame_counts");
    nonnull(param_values, "param_values");

    for (int i = 0; i < num_iterations; i++)
    {
        param_values[i] = opt_param_min + i * opt_param_step;
        modem_simple_init(&modems[i], sample_rate, param_values[i]);
        frame_counts[i] = 0;
    }

    const size_t max_chunk_bytes = 1024 * 1024;
    const size_t chunk_size_samples = max_chunk_bytes / sizeof(float);
    LOG("processing in chunks of %zu samples", chunk_size_samples);

    clock_t start_time = clock();

    for (size_t chunk_offset = 0; chunk_offset < samples_len; chunk_offset += chunk_size_samples)
    {
        size_t chunk_remaining = samples_len - chunk_offset;
        size_t chunk_samples = (chunk_remaining < chunk_size_samples) ? chunk_remaining : chunk_size_samples;

        for (int i = 0; i < num_iterations; i++)
        {
            int chunk_frames = evaluate_modem(&modems[i], &samples_buf, chunk_offset, chunk_samples);
            frame_counts[i] += chunk_frames;
        }

        clock_t now = clock();
        double elapsed = (double)(now - start_time) / CLOCKS_PER_SEC;
        double percent = (100.0 * (chunk_offset + chunk_samples)) / samples_len;
        double rate = (elapsed > 0) ? (percent / elapsed) : 0;
        fprintf(stderr, "\rprocessing... %.1f%% (%.2f%%/sec)  ", percent, rate);
        fflush(stderr);
    }

    clock_t end_time = clock();
    double total_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    fprintf(stderr, "\ncompleted in %.2f seconds\n", total_time);

    for (int i = 0; i < num_iterations; i++)
        printf("%.4f;%d\n", param_values[i], frame_counts[i]);

    for (int i = 0; i < num_iterations; i++)
        modem_simple_free(&modems[i]);

    free(modems);
    free(frame_counts);
    free(param_values);
    free(samples);

    return exit_code;

ERROR:
    exit_code = EXIT_FAILURE;
    return exit_code;
}
