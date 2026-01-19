#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "options.h"

static struct argp_option options[] = {
    {OPT_CONFIG, OPT_SHORT_CONFIG, "FILE", 0, "Configuration file", 1},

    {OPT_LIST, OPT_SHORT_LIST, 0, 0, "List audio devices and exit", 2},
    {OPT_VERBOSE, OPT_SHORT_VERBOSE, 0, 0, "Enable verbose logs", 2},
    {OPT_DEBUG, OPT_SHORT_DEBUG, 0, 0, "Enable verbose and debugging logs", 2},
    {OPT_NOOP, OPT_SHORT_NOOP, 0, 0, "Do not enter main processing loop", 2},
    {OPT_KISS, OPT_SHORT_KISS, 0, 0, "Use KISS protocol instead of TNC2", 2},

    {OPT_DEV_NAME, OPT_SHORT_DEV_NAME, "NAME", 0, "Sound device name", 3},
    {OPT_DEV_INPUT, OPT_SHORT_DEV_INPUT, 0, 0, "Use sound device for input", 3},
    {OPT_DEV_OUTPUT, OPT_SHORT_DEV_OUTPUT, 0, 0, "Use sound device for output", 3},
    {OPT_RATE, OPT_SHORT_RATE, "RATE", 0, "Sample rate (default: 44100Hz)", 3},

    {OPT_TCP_KISS_PORT, OPT_SHORT_TCP_KISS_PORT, "PORT", 0, "TCP server port in KISS format", 4},
    {OPT_TCP_TNC2_PORT, OPT_SHORT_TCP_TNC2_PORT, "PORT", 0, "TCP server port in TNC2 format", 4},

    {OPT_UDP_KISS_ADDR, OPT_SHORT_UDP_KISS_ADDR, "ADDR", 0, "UDP destination address for KISS packets", 4},
    {OPT_UDP_KISS_PORT, OPT_SHORT_UDP_KISS_PORT, "PORT", 0, "UDP destination port for KISS packets", 4},
    {OPT_UDP_TNC2_ADDR, OPT_SHORT_UDP_TNC2_ADDR, "ADDR", 0, "UDP destination address for TNC2 packets", 4},
    {OPT_UDP_TNC2_PORT, OPT_SHORT_UDP_TNC2_PORT, "PORT", 0, "UDP destination port for TNC2 packets", 4},

    {OPT_UDP_KISS_LISTEN_PORT, OPT_SHORT_UDP_KISS_LISTEN_PORT, "PORT", 0, "UDP server port for receiving KISS packets", 4},
    {OPT_UDP_TNC2_LISTEN_PORT, OPT_SHORT_UDP_TNC2_LISTEN_PORT, "PORT", 0, "UDP server port for receiving TNC2 packets", 4},

    {OPT_SQUELCH, OPT_SHORT_SQUELCH, 0, 0, "Enable pseudo-squelch", 5},
    {OPT_GAIN_2200, OPT_SHORT_GAIN_2200, "GAIN", 0, "Equalization to apply at 2200 Hz [dB]", 5},
    {OPT_TX_DELAY, OPT_SHORT_TX_DELAY, "MS", 0, "Time to send flags before a packet (default: 300ms)", 5},
    {OPT_TX_TAIL, OPT_SHORT_TX_TAIL, "MS", 0, "Time to send flags after a packet (default: 30ms)", 5},
    {OPT_EXIT_IDLE_S, OPT_SHORT_EXIT_IDLE_S, "S", 0, "Exit the program if no packets received in S seconds", 5},

    {0, 0, 0, 0, 0, 0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    options_t *opts = state->input;
    switch (key)
    {
    case OPT_SHORT_CONFIG:
        strncpy(opts->config_file, arg, OPT_STR_SIZE - 1);
        break;
    case OPT_SHORT_LIST:
        opts->list = true;
        break;
    case OPT_SHORT_VERBOSE:
        opts->log_level = LOG_LEVEL_VERBOSE;
        break;
    case OPT_SHORT_DEBUG:
        opts->log_level = LOG_LEVEL_DEBUG;
        break;
    case OPT_SHORT_NOOP:
        opts->noop = true;
        break;
    case OPT_SHORT_KISS:
        opts->kiss = true;
        break;
    case OPT_SHORT_DEV_NAME:
        strncpy(opts->dev_name, arg, OPT_STR_SIZE - 1);
        break;
    case OPT_SHORT_DEV_INPUT:
        opts->dev_input = true;
        break;
    case OPT_SHORT_DEV_OUTPUT:
        opts->dev_output = true;
        break;
    case OPT_SHORT_TCP_KISS_PORT:
        opts->tcp_kiss_port = atoi(arg);
        break;
    case OPT_SHORT_TCP_TNC2_PORT:
        opts->tcp_tnc2_port = atoi(arg);
        break;
    case OPT_SHORT_UDP_KISS_ADDR:
        strncpy(opts->udp_kiss_addr, arg, OPT_STR_SIZE - 1);
        break;
    case OPT_SHORT_UDP_KISS_PORT:
        opts->udp_kiss_port = atoi(arg);
        break;
    case OPT_SHORT_UDP_TNC2_ADDR:
        strncpy(opts->udp_tnc2_addr, arg, OPT_STR_SIZE - 1);
        break;
    case OPT_SHORT_UDP_TNC2_PORT:
        opts->udp_tnc2_port = atoi(arg);
        break;
    case OPT_SHORT_UDP_KISS_LISTEN_PORT:
        opts->udp_kiss_listen_port = atoi(arg);
        break;
    case OPT_SHORT_UDP_TNC2_LISTEN_PORT:
        opts->udp_tnc2_listen_port = atoi(arg);
        break;
    case OPT_SHORT_RATE:
        opts->rate = atoi(arg);
        break;
    case OPT_SHORT_SQUELCH:
        opts->squelch = 1;
        break;
    case OPT_SHORT_GAIN_2200:
        opts->gain_2200 = atof(arg);
        break;
    case OPT_SHORT_TX_DELAY:
        opts->tx_delay = atof(arg);
        break;
    case OPT_SHORT_TX_TAIL:
        opts->tx_tail = atof(arg);
        break;
    case OPT_SHORT_EXIT_IDLE_S:
        opts->exit_idle_s = atoi(arg);
        break;
    case ARGP_KEY_NO_ARGS:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

struct argp argp = {
    options,
    parse_opt,
    "",
    "Minimal soundcard modem/TNC"};

void opts_parse_args(options_t *opts, int argc, char *argv[])
{
    nonnull(opts, "opts");
    nonzero(argc, "argc");
    nonnull(argv, "argv");

    argp_parse(&argp, argc, argv, 0, 0, opts);
}
