#include <argp.h>
#include <stdlib.h>
#include "common.h"
#include "args.h"

static struct argp_option options[] = {
    // Modes
    {"list", 'l', 0, 0, "List audio devices and exit", 1},
    {"verbose", 'v', 0, 0, "Enable verbose logs", 1},
    {"debug", 'V', 0, 0, "Enable verbose and debugging logs", 1},
    {"noop", 'x', 0, 0, "Do not enter main processing loop", 1},
    {"kiss", 'k', 0, 0, "Use KISS protocol instead of TNC2", 1},

    // Sound device options
    {"dev", 'd', "NAME", 0, "Sound device name", 2},
    {"dev-index", 'D', "INDEX", 0, "Sound device index", 2},
    {"input", 'i', 0, 0, "Use sound device for input", 2},
    {"output", 'o', 0, 0, "Use sound device for output", 2},
    {"rate", 'r', "RATE", 0, "Sample rate for both input and output", 2},

    // TCP servers
    {"tcp-kiss", '\x01', "PORT", 0, "TCP server port in KISS format", 3},
    {"tcp-tnc2", '\x02', "PORT", 0, "TCP server port in TNC2 format", 3},

    // UDP destinations
    {"udp-kiss-addr", '\x04', "ADDR", 0, "UDP destination address for KISS packets", 3},
    {"udp-kiss-port", '\x05', "PORT", 0, "UDP destination port for KISS packets", 3},
    {"udp-tnc2-addr", '\x06', "ADDR", 0, "UDP destination address for TNC2 packets", 3},
    {"udp-tnc2-port", '\x07', "PORT", 0, "UDP destination port for TNC2 packets", 3},

    // UDP servers
    {"udp-kiss-listen", '\x08', "PORT", 0, "UDP server port for receiving KISS packets", 3},
    {"udp-tnc2-listen", '\x09', "PORT", 0, "UDP server port for receiving TNC2 packets", 3},

    // Other options
    {"squelch", 's', 0, 0, "Enable pseudo-squelch", 4},
    {"eq2200", '2', "GAIN", 0, "Equalization to apply at 2200 Hz [dB]", 4},
    {"tx-delay", 'y', "MS", 0, "Time to send flags before a packet (default: 300ms)", 4},
    {"tx-tail", 'z', "MS", 0, "Time to send flags after a packet (default: 50ms)", 4},
    {"exit-idle", '\x03', "S", 0, "Exit the program if no packets received in S seconds", 4},

    // end
    {0, 0, 0, 0, 0, 0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    args_t *args = state->input;
    switch (key)
    {
    case 'l':
        args->list = 1;
        break;
    case 'v':
        args->log_level = LOG_LEVEL_VERBOSE;
        break;
    case 'V':
        args->log_level = LOG_LEVEL_DEBUG;
        break;
    case 'x':
        args->noop = 1;
        break;
    case 'k':
        args->kiss = 1;
        break;
    case 'd':
        args->dev_name = arg;
        break;
    case 'i':
        args->dev_input = 1;
        break;
    case 'o':
        args->dev_output = 1;
        break;
    case '\x01':
        args->tcp_kiss_port = atoi(arg);
        break;
    case '\x02':
        args->tcp_tnc2_port = atoi(arg);
        break;
    case '\x04':
        args->udp_kiss_addr = arg;
        break;
    case '\x05':
        args->udp_kiss_port = atoi(arg);
        break;
    case '\x06':
        args->udp_tnc2_addr = arg;
        break;
    case '\x07':
        args->udp_tnc2_port = atoi(arg);
        break;
    case '\x08':
        args->udp_kiss_listen_port = atoi(arg);
        break;
    case '\x09':
        args->udp_tnc2_listen_port = atoi(arg);
        break;
    case 'r':
        args->rate = atoi(arg);
        break;
    case 's':
        args->squelch = 1;
        break;
    case '2':
        args->gain_2200 = atof(arg);
        break;
    case 'y':
        args->tx_delay = atof(arg);
        break;
    case 'z':
        args->tx_tail = atof(arg);
        break;
    case '\x03':
        args->exit_idle_s = atoi(arg);
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

void args_parse(int argc, char *argv[], args_t *args)
{
    nonnull(args, "args");

    args->tx_delay = 300.0f;
    args->tx_tail = 50.0f;
    args->exit_idle_s = INT_MAX;

    argp_parse(&argp, argc, argv, 0, 0, args);

    // Validate TCP ports
    if (args->tcp_kiss_port > 0 && args->tcp_tnc2_port > 0 && args->tcp_kiss_port == args->tcp_tnc2_port)
        EXIT("tcp-kiss and tcp-tnc2 ports must be different");
}
