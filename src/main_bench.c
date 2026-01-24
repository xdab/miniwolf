#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <argp.h>
#include "modem.h"
#include "ax25.h"
#include "tnc2.h"
#include "squelch.h"
#include "filter.h"
#include "common.h"

#define CHUNK_SIZE 2048

typedef struct bench_args
{
    char *input_file;
    int rate;
    log_level_e log_level;
    char type;
    int bits;
    int little_endian;
    float gain_2200;
    int use_squelch;
    int save_squelched;
} bench_args_t;

static void byteswap16(uint16_t *v)
{
    *v = (((*v & 0xFF) << 8) | ((*v >> 8) & 0xFF));
}

static void byteswap32(uint32_t *v)
{
    *v = (((*v & 0xFF) << 24) | (((*v >> 8) & 0xFF) << 16) |
          (((*v >> 16) & 0xFF) << 8) | ((*v >> 24) & 0xFF));
}

static void byteswap64(uint64_t *v)
{
    *v = (((*v & 0xFF) << 56) | (((*v >> 8) & 0xFF) << 48) |
          (((*v >> 16) & 0xFF) << 40) | (((*v >> 24) & 0xFF) << 32) |
          (((*v >> 32) & 0xFF) << 24) | (((*v >> 40) & 0xFF) << 16) |
          (((*v >> 48) & 0xFF) << 8) | ((*v >> 56) & 0xFF));
}

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

static struct argp_option bench_options[] = {
    {"file", 'f', "FILE", 0, "Input raw audio file (required)", 1},
    {"rate", 'r', "RATE", 0, "Sample rate in Hz (default: 48000)", 1},
    {"format", 'F', "FORMAT", 0, "Audio format: F32, F64, S8, S16, S32, U8, U16, U32 (default: F32)", 2},
    {"endian", 'e', "ENDIAN", 0, "Byte order: LE (little-endian, default) or BE (big-endian)", 2},
    {"eq2200", '2', "GAIN", 0, "Extra gain to apply at 2200Hz in dB (default: 0.0)", 2},
    {"squelch", 's', 0, 0, "Enable squelch processing", 2},
    {"save-squelched", 'S', 0, 0, "Save squelched audio to squelched_<input>.raw (requires --squelch)", 3},
    {"verbose", 'v', 0, 0, "Enable verbose logs", 3},
    {"debug", 'V', 0, 0, "Enable verbose and debugging logs", 3},
    {0, 0, 0, 0, 0, 0}};

static error_t bench_parse_opt(int key, char *arg, struct argp_state *state)
{
    bench_args_t *args = state->input;
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
    case 's':
        args->use_squelch = 1;
        break;
    case 'S':
        args->save_squelched = 1;
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

static struct argp bench_argp = {
    bench_options,
    bench_parse_opt,
    "",
    "Offline Bell202 demodulator for raw audio files"};

static void bench_args_parse(int argc, char *argv[], bench_args_t *args)
{
    args->rate = 48000;
    args->log_level = LOG_LEVEL_STANDARD;
    args->input_file = NULL;
    args->type = 'F';
    args->bits = 32;
    args->little_endian = 1;
    args->gain_2200 = 0.0f;
    args->use_squelch = 0;
    args->save_squelched = 0;

    argp_parse(&bench_argp, argc, argv, 0, 0, args);
}

int main(int argc, char *argv[])
{
    int exit_code = EXIT_SUCCESS;
    bench_args_t args = {0};
    bench_args_parse(argc, argv, &args);
    _log_level = args.log_level;
    _func_pad = -1;

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

    if (args.save_squelched && !args.use_squelch)
    {
        LOG("Error: --save-squelched requires --squelch.");
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

    FILE *sq_fp = NULL;
    char sq_filename[512];
    if (args.save_squelched)
    {
        snprintf(sq_filename, sizeof(sq_filename), "squelched_%s", args.input_file);
        sq_fp = fopen(sq_filename, "wb");
        if (!sq_fp)
        {
            fprintf(stderr, "Error: Cannot open file '%s'.\n", sq_filename);
            goto ERROR;
        }
    }

    // Channel equalization
    bf_biquad_t g_hbf_filter;
    bf_hbf_init(&g_hbf_filter, 4, 2200.0f, sample_rate, args.gain_2200);

    // Initialize modem
    modem_t modem;
    modem_params_t modem_params = {
        .sample_rate = sample_rate,
        .types = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE,
        .tx_delay = 300.0f,
        .tx_tail = 50.0f};
    modem_init(&modem, &modem_params);

    // Initialize squelch (only if enabled)
    sql_t squelch;
    sql_params_t sql_params = {
        .sample_rate = sample_rate,
        .init_threshold = 0.045f,
        .strength = 0.51f};
    if (args.use_squelch)
        sql_init(&squelch, &sql_params, &sql_params_default);

    // Process file
    uint8_t raw_buffer[CHUNK_SIZE * 32];
    float samples[CHUNK_SIZE];
    uint8_t frame_buffer[512];
    int packet_count = 0;
    uint64_t total_samples = 0;

    LOGV("Processing file...");

    clock_t total_time = 0;

    for (;;)
    {
        size_t read_count = fread(raw_buffer, bytes_per_sample, CHUNK_SIZE, fp);
        if (read_count == 0)
            break;

        for (size_t i = 0; i < read_count; i++)
        {
            samples[i] = convert_sample(
                &raw_buffer[i * bytes_per_sample],
                args.type,
                args.bits,
                args.little_endian);

            // Apply high boost channel equalization
            samples[i] = bf_biquad_filter(&g_hbf_filter, samples[i]);

            // Apply squelch (only if enabled)
            if (args.use_squelch)
            {
                int squelch_open = sql_process(&squelch, samples[i]);
                if (!squelch_open)
                    samples[i] = 0.0f; // Zero out samples when squelch is closed

                // Save squelched samples if requested
                if (sq_fp && squelch_open)
                    fwrite(&samples[i], sizeof(float), 1, sq_fp);
            }
        }

        total_samples += read_count;

        // Demodulate with timing
        clock_t start, end;
        start = clock();
        float_buffer_t sample_buf = {.data = samples, .capacity = CHUNK_SIZE, .size = read_count};
        buffer_t frame_buf = {.data = frame_buffer, .capacity = sizeof(frame_buffer), .size = 0};
        int frame_len = modem_demodulate(&modem, &sample_buf, &frame_buf);
        end = clock();
        total_time += end - start;

        if (frame_len <= 0)
            continue;

        // Print with counter and timestamp
        double time_sec = (double)total_samples / sample_rate;

        // Unpack AX.25 packet
        ax25_packet_t packet;
        if (ax25_packet_unpack(&packet, &frame_buf) != 0)
        {
            LOGV("Warning: invalid AX.25 packet (%d bytes) at %.3f s", frame_len, time_sec);
            continue;
        }

        packet_count++;

        // Convert to TNC2 format
        char tnc2_data[512];
        buffer_t tnc2_buf = {
            .data = (unsigned char *)tnc2_data,
            .capacity = sizeof(tnc2_data),
            .size = 0};
        int tnc2_len = tnc2_packet_to_string(&packet, &tnc2_buf);
        if (tnc2_len <= 0)
        {
            LOG("Warning: invalid TNC2 conversion for packet at %.3f s", time_sec);
            continue;
        }
        uint64_t hours = (uint64_t)time_sec / 3600;
        uint64_t mins = ((uint64_t)time_sec % 3600) / 60;
        uint64_t secs = (uint64_t)time_sec % 60;
        uint64_t ms = (uint64_t)(fmod(time_sec * 1000.0, 1000.0));

        LOG("%d @ %02lu:%02lu:%02lu.%03lu %s", packet_count, hours, mins, secs, ms, tnc2_data);
        if (args.use_squelch)
            LOGV("sql threshold %.4f, high ema %.4f, low ema %.4f",
                 squelch.threshold, squelch.high_ema, squelch.low_ema);
    }

    LOG("Packets: %d", packet_count);
    LOG("Time in modem_demodulate: %.3f s", (float)total_time / CLOCKS_PER_SEC);

    // Cleanup
    if (sq_fp)
        fclose(sq_fp);
    fclose(fp);
    modem_free(&modem);
    bf_biquad_free(&g_hbf_filter);

    return exit_code;

ERROR:
    exit_code = EXIT_FAILURE;
    return exit_code;
}
