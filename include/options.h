#pragma once

#include "common.h"
#include <stdbool.h>

#define OPT_CONFIG "config"
#define OPT_LIST "list"
#define OPT_VERBOSE "verbose"
#define OPT_DEBUG "debug"
#define OPT_NOOP "noop"
#define OPT_KISS "kiss"
#define OPT_LOG_LEVEL "log-level"
#define OPT_DEV_NAME "dev"
#define OPT_DEV_INDEX "dev-index"
#define OPT_DEV_INPUT "input"
#define OPT_DEV_OUTPUT "output"
#define OPT_RATE "rate"
#define OPT_TCP_KISS_PORT "tcp-kiss"
#define OPT_TCP_TNC2_PORT "tcp-tnc2"
#define OPT_UDP_KISS_ADDR "udp-kiss-addr"
#define OPT_UDP_KISS_PORT "udp-kiss-port"
#define OPT_UDP_TNC2_ADDR "udp-tnc2-addr"
#define OPT_UDP_TNC2_PORT "udp-tnc2-port"
#define OPT_UDP_KISS_LISTEN_PORT "udp-kiss-listen"
#define OPT_UDP_TNC2_LISTEN_PORT "udp-tnc2-listen"
#define OPT_SQUELCH "squelch"
#define OPT_GAIN_2200 "eq2200"
#define OPT_TX_DELAY "tx-delay"
#define OPT_TX_TAIL "tx-tail"
#define OPT_EXIT_IDLE_S "exit-idle"

#define OPT_SHORT_CONFIG 'c'
#define OPT_SHORT_LIST 'l'
#define OPT_SHORT_VERBOSE 'v'
#define OPT_SHORT_DEBUG 'V'
#define OPT_SHORT_NOOP 'x'
#define OPT_SHORT_KISS 'k'
#define OPT_SHORT_DEV_NAME 'd'
#define OPT_SHORT_DEV_INPUT 'i'
#define OPT_SHORT_DEV_OUTPUT 'o'
#define OPT_SHORT_RATE 'r'
#define OPT_SHORT_TCP_KISS_PORT '\x01'
#define OPT_SHORT_TCP_TNC2_PORT '\x02'
#define OPT_SHORT_UDP_KISS_ADDR '\x04'
#define OPT_SHORT_UDP_KISS_PORT '\x05'
#define OPT_SHORT_UDP_TNC2_ADDR '\x06'
#define OPT_SHORT_UDP_TNC2_PORT '\x07'
#define OPT_SHORT_UDP_KISS_LISTEN_PORT '\x08'
#define OPT_SHORT_UDP_TNC2_LISTEN_PORT '\x09'
#define OPT_SHORT_SQUELCH 's'
#define OPT_SHORT_GAIN_2200 '2'
#define OPT_SHORT_TX_DELAY 'y'
#define OPT_SHORT_TX_TAIL 'z'
#define OPT_SHORT_EXIT_IDLE_S '\x03'

#define OPT_STR_SIZE 256

typedef struct options
{
    char config_file[OPT_STR_SIZE];

    bool list;
    bool noop;
    bool kiss;
    log_level_e log_level;

    char dev_name[OPT_STR_SIZE];
    bool dev_input;
    bool dev_output;
    int rate;

    int tcp_kiss_port;
    int tcp_tnc2_port;

    char udp_kiss_addr[OPT_STR_SIZE];
    int udp_kiss_port;
    char udp_tnc2_addr[OPT_STR_SIZE];
    int udp_tnc2_port;

    int udp_kiss_listen_port;
    int udp_tnc2_listen_port;

    bool squelch;
    float gain_2200;
    float tx_delay;
    float tx_tail;
    long exit_idle_s;
} options_t;

// Clears out options_t setting null/zero values.
void opts_init(options_t *opts);

// Parses command line arguments to options_t, OVERWRITING ALL VALUES
void opts_parse_args(options_t *opts, int argc, char *argv[]);

// Parses file to options_t, overwriting only null/zero/empty values
void opts_parse_conf_file(options_t *opts, const char *filename);

// Applies default values, overwriting only null/zero/empty values
void opts_defaults(options_t *opts);
