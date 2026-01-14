#include "miniwolf.h"
#include <unistd.h>
#include <fcntl.h>

miniwolf_t g_miniwolf;

extern void tnc2_input_callback(const buffer_t *line_buf);

void miniwolf_init(miniwolf_t *mw, const args_t *args)
{
    nonnull(mw, "mw");
    nonnull(args, "args");

    mw->max_idle_time = args->exit_idle_s;
    mw->last_packet_time = time(NULL);

    mw->squelch_enabled = args->squelch;
    mw->kiss_mode = args->kiss;
    float sample_rate = (float)args->rate;

    // TCP servers
    mw->tcp_kiss_enabled = 0;
    if (args->tcp_kiss_port > 0 && !tcp_server_init(&mw->tcp_kiss_server, args->tcp_kiss_port))
    {
        mw->tcp_kiss_enabled = 1;
        LOG("tcp kiss server enabled on port %d", args->tcp_kiss_port);
    }

    mw->tcp_tnc2_enabled = 0;
    if (args->tcp_tnc2_port > 0 && !tcp_server_init(&mw->tcp_tnc2_server, args->tcp_tnc2_port))
    {
        mw->tcp_tnc2_enabled = 1;
        LOG("tcp tnc2 server enabled on port %d", args->tcp_tnc2_port);
    }

    // UDP senders
    mw->udp_kiss_enabled = 0;
    if (args->udp_kiss_addr && args->udp_kiss_port > 0 && !udp_sender_init(&mw->udp_kiss_sender, args->udp_kiss_addr, args->udp_kiss_port))
    {
        mw->udp_kiss_enabled = 1;
        LOG("udp kiss sender enabled to %s:%d", args->udp_kiss_addr, args->udp_kiss_port);
    }

    mw->udp_tnc2_enabled = 0;
    if (args->udp_tnc2_addr && args->udp_tnc2_port > 0 && !udp_sender_init(&mw->udp_tnc2_sender, args->udp_tnc2_addr, args->udp_tnc2_port))
    {
        mw->udp_tnc2_enabled = 1;
        LOG("udp tnc2 sender enabled to %s:%d", args->udp_tnc2_addr, args->udp_tnc2_port);
    }

    // UDP servers
    mw->udp_kiss_listen_enabled = 0;
    if (args->udp_kiss_listen_port > 0 && !udp_server_init(&mw->udp_kiss_server, args->udp_kiss_listen_port))
    {
        mw->udp_kiss_listen_enabled = 1;
        LOG("udp kiss server enabled on port %d", args->udp_kiss_listen_port);
    }

    mw->udp_tnc2_listen_enabled = 0;
    if (args->udp_tnc2_listen_port > 0 && !udp_server_init(&mw->udp_tnc2_server, args->udp_tnc2_listen_port))
    {
        mw->udp_tnc2_listen_enabled = 1;
        LOG("udp tnc2 server enabled on port %d", args->udp_tnc2_listen_port);
    }

    // Make stdin non-blocking
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    if (args->gain_2200 != 0.0f)
    {
        LOGV("enabling high boost filter with gain of %f dB", args->gain_2200);
        bf_hbf_init(&mw->hbf_filter, 4, 2200.0f, sample_rate, args->gain_2200);
    }

    modem_params_t modem_params = {
        .sample_rate = sample_rate,
        .types = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE,
        .tx_delay = args->tx_delay,
        .tx_tail = args->tx_tail};
    modem_init(&mw->modem, &modem_params);

    agc_init(&mw->input_agc, 10.0, 60e3f, sample_rate);
    sql_init(&mw->squelch, 0.05f, 60e3f, sample_rate);
    kiss_decoder_init(&mw->kiss_decoder);
    line_reader_init(&mw->line_reader, tnc2_input_callback);
    line_reader_init(&mw->tcp_tnc2_line_reader, tnc2_input_callback);
}

void miniwolf_free(miniwolf_t *mw)
{
    if (mw->tcp_kiss_enabled)
        tcp_server_free(&mw->tcp_kiss_server);
    if (mw->tcp_tnc2_enabled)
        tcp_server_free(&mw->tcp_tnc2_server);

    if (mw->udp_kiss_enabled)
        udp_sender_free(&mw->udp_kiss_sender);
    if (mw->udp_tnc2_enabled)
        udp_sender_free(&mw->udp_tnc2_sender);

    if (mw->udp_kiss_listen_enabled)
        udp_server_free(&mw->udp_kiss_server);
    if (mw->udp_tnc2_listen_enabled)
        udp_server_free(&mw->udp_tnc2_server);

    modem_free(&mw->modem);
    bf_biquad_free(&mw->hbf_filter);
}
