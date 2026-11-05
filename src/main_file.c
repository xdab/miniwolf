#include "ax25.h"
#include "buffer.h"
#include "common.h"
#include "demod.h"
#include "device.h"
#include "rx.h"
#include "tnc2.h"
#include <argp.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHUNK_SIZE 2048

typedef struct file_args
{
    char *input_file;
    int rate;
    log_level_e log_level;
    char type;
    int bits;
    int little_endian;
    float gain_2200;
    int use_squelch;
    float squelch_strength;
} file_args_t;

static struct argp_option file_options[] = {
    {"file", 'f', "FILE", 0, "Input raw or WAV audio file (required)", 1},
    {"rate", 'r', "RATE", 0, "Sample rate in Hz (default: 48000, WAV: from header)", 1},
    {"format",
     'F',
     "FORMAT",
     0,
     "Raw audio format: F32, F64, S8, S16, S32, U8, U16, U32 (default: F32, WAV: from header)",
     2},
    {"endian", 'e', "ENDIAN", 0, "Byte order: LE (little-endian, default) or BE (big-endian)", 2},
    {"eq2200", '2', "GAIN", 0, "Extra gain to apply at 2200Hz in dB (default: 0.0)", 2},
    {"squelch", 's', "STRENGTH", 0, "Enable squelch with given strength (0.0-1.0)", 2},
    {"verbose", 'v', 0, 0, "Enable verbose logs", 3},
    {"debug", 'V', 0, 0, "Enable verbose and debugging logs", 3},
    {0, 0, 0, 0, 0, 0}};

static error_t file_parse_opt(int key, char *arg, struct argp_state *state)
{
    file_args_t *args = state->input;
    switch (key)
    {
    case 'f':
        args->input_file = arg;
        break;
    case 'r':
        args->rate = (int)strtol(arg, NULL, 10);
        break;
    case 'F':
        if (strlen(arg) >= 2)
        {
            args->type = arg[0];
            args->bits = (int)strtol(&arg[1], NULL, 10);
        }
        break;
    case 'e':
        if (strcmp(arg, "BE") == 0 || strcmp(arg, "be") == 0)
            args->little_endian = 0;
        else if (strcmp(arg, "LE") == 0 || strcmp(arg, "le") == 0)
            args->little_endian = 1;
        break;
    case '2':
        args->gain_2200 = (float)strtod(arg, NULL);
        break;
    case 's':
        args->use_squelch = 1;
        args->squelch_strength = (float)strtod(arg, NULL);
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

static struct argp file_argp = {
    file_options, file_parse_opt, "", "Offline Bell202 demodulator for raw or WAV audio files"};

static void file_args_parse(int argc, char *argv[], file_args_t *args)
{
    args->rate = 48000;
    args->log_level = LOG_LEVEL_STANDARD;
    args->input_file = NULL;
    args->type = 0;
    args->bits = 0;
    args->little_endian = -1;
    args->gain_2200 = 0.0f;
    args->use_squelch = 0;
    args->squelch_strength = 0.50f;

    argp_parse(&file_argp, argc, argv, 0, 0, args);
    // type/bits/little_endian stay 0/0/-1 when no -F/-e was given so a WAV
    // header can supply them; the file backend defaults raw input to F32 LE
}

int main(int argc, char *argv[])
{
    int exit_code = EXIT_SUCCESS;
    file_args_t args = {0};
    file_args_parse(argc, argv, &args);
    g_log_level = args.log_level;
    g_log_func_pad = -1;

    if (!args.input_file)
    {
        LOG("Error: --file <input_file> required.");
        return EXIT_FAILURE;
    }

    if (args.rate <= 0)
    {
        LOG("Error: Invalid sample rate.");
        return EXIT_FAILURE;
    }

    device_file_params_t dev_params = {.type = args.type,
                                       .bits = args.bits,
                                       .little_endian = args.little_endian,
                                       .rate = args.rate};
    device_t *dev = device_open_file(args.input_file, &dev_params);
    if (!dev)
        return EXIT_FAILURE;

    if ((dev_params.type == 'F' && (dev_params.bits != 32 && dev_params.bits != 64)) ||
        (dev_params.type != 'F' &&
         (dev_params.bits != 8 && dev_params.bits != 16 && dev_params.bits != 32)))
    {
        LOG("Error: Invalid format. F: 32/64, S/U: 8/16/32.");
        device_free(dev);
        return EXIT_FAILURE;
    }

    float sample_rate = (float)dev_params.rate;

    LOGV("Sample rate: %.0f Hz", sample_rate);
    LOGV(
        "Format: %c%d %s",
        dev_params.type,
        dev_params.bits,
        dev_params.little_endian ? "LE" : "BE");

    rx_params_t rx_params = {.sample_rate = sample_rate,
                             .gain_2200 = args.gain_2200,
                             .use_squelch = args.use_squelch,
                             .squelch_strength = args.squelch_strength,
                             .types = DEMOD_QUADRATURE | DEMOD_ALL_GOERTZEL};
    rx_chain_t rx;
    rx_chain_init(&rx, &rx_params);

    float samples[CHUNK_SIZE];
    uint8_t frame_buffer[512];
    int packet_count = 0;
    uint64_t total_samples = 0;

    LOGV("Processing file...");

    clock_t total_time = 0;
    time_t time_zero = time(NULL);

    for (;;)
    {
        int n = device_read(dev, samples, CHUNK_SIZE);
        if (n <= 0)
            break;

        total_samples += n;
        double time_sec = (double)total_samples / sample_rate;

        // Demodulate with timing
        buffer_t frame_buf = {.data = frame_buffer, .capacity = sizeof(frame_buffer), .size = 0};
        clock_t start, end;
        time_t time_sim = time_zero + (time_t)time_sec;
        start = clock();
        int frame_len = rx_chain_process(&rx, samples, n, time_sim, &frame_buf);
        end = clock();
        total_time += end - start;

        if (frame_len <= 0)
            continue;

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
            .data = (unsigned char *)tnc2_data, .capacity = sizeof(tnc2_data), .size = 0};
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
            LOGV(
                "sql threshold %.4f, high ema %.4f, low ema %.4f",
                rx.squelch.threshold,
                rx.squelch.high_ema,
                rx.squelch.low_ema);
    }

    LOG("Packets: %d", packet_count);
    LOG("Time in modem_demodulate: %.3f s", (float)total_time / CLOCKS_PER_SEC);

    rx_chain_free(&rx);
    device_free(dev);

    return exit_code;
}
