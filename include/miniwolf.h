#pragma once

#include "options.h"
#include "agc.h"
#include "squelch.h"
#include "modem.h"
#include "line.h"
#include "kiss.h"
#include "filter.h"
#include "tcp.h"
#include "udp.h"
#include "uds.h"
#include "socket.h"
#include <time.h>

typedef struct miniwolf_state
{
    // DSP components
    agc_t input_agc;
    sql_t squelch;
    modem_t modem;
    bf_biquad_t hbf_filter;

    // Input readers
    line_reader_t stdin_line_reader;
    line_reader_t tcp_line_reader;
    line_reader_t udp_line_reader;
    line_reader_t uds_line_reader;
    kiss_decoder_t kiss_decoder;

    // Network servers and senders
    tcp_server_t tcp_kiss_server;
    tcp_server_t tcp_tnc2_server;
    udp_sender_t udp_kiss_sender;
    udp_sender_t udp_tnc2_sender;
    udp_server_t udp_kiss_server;
    udp_server_t udp_tnc2_server;
    uds_server_t uds_kiss_server;
    uds_server_t uds_tnc2_server;
    socket_selector_t selector;

    // Configuration flags
    int kiss_mode;
    int tcp_kiss_enabled;
    int tcp_tnc2_enabled;
    int udp_kiss_enabled;
    int udp_tnc2_enabled;
    int udp_kiss_listen_enabled;
    int udp_tnc2_listen_enabled;
    int uds_kiss_enabled;
    int uds_tnc2_enabled;
    int squelch_enabled;

    // Timing
    time_t max_idle_time;
    time_t last_packet_time;
} miniwolf_t;

extern miniwolf_t g_miniwolf;

void miniwolf_init(miniwolf_t *mw, const options_t *opts);

void miniwolf_free(miniwolf_t *mw);
