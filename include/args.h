#ifndef ARGS_H
#define ARGS_H

#include "common.h"

typedef struct arguments
{
    // Modes
    int list;
    int noop;
    int kiss;
    log_level_e log_level;

    // Soundcard
    char *dev_name;
    int dev_input;
    int dev_output;
    int rate;

    // TCP servers
    int tcp_kiss_port;
    int tcp_tnc2_port;

    // UDP destinations
    char *udp_kiss_addr;
    int udp_kiss_port;
    char *udp_tnc2_addr;
    int udp_tnc2_port;

    // UDP servers
    int udp_kiss_listen_port;
    int udp_tnc2_listen_port;

    // Other options
    int squelch;
    float gain_2200;
    float tx_delay;
    float tx_tail;
    int exit_idle_s;

} args_t;

void args_parse(int argc, char **argv, args_t *args);

#endif
