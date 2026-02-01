#include "miniwolf.h"
#include "options.h"
#include <unistd.h>
#include <fcntl.h>

miniwolf_t g_miniwolf;

extern void tnc2_input_callback(const buffer_t *line_buf);

void miniwolf_init(miniwolf_t *mw, const options_t *opts)
{
    nonnull(mw, "mw");
    nonnull(opts, "opts");

    mw->max_idle_time = opts->exit_idle_s;
    mw->last_packet_time = time(NULL);

    mw->squelch_enabled = opts->squelch > 0.0f;
    mw->kiss_mode = opts->kiss;
    float sample_rate = (float)opts->rate;

    socket_selector_init(&mw->selector);

    // TCP servers
    mw->tcp_kiss_enabled = 0;
    if (opts->tcp_kiss_port > 0 && !tcp_server_init(&mw->tcp_kiss_server, opts->tcp_kiss_port, 0))
    {
        mw->tcp_kiss_enabled = 1;
        socket_selector_add(&mw->selector, mw->tcp_kiss_server.listen_fd, SELECT_READ);
        LOG("tcp kiss server enabled on port %d", opts->tcp_kiss_port);
    }

    mw->tcp_tnc2_enabled = 0;
    if (opts->tcp_tnc2_port > 0 && !tcp_server_init(&mw->tcp_tnc2_server, opts->tcp_tnc2_port, 0))
    {
        mw->tcp_tnc2_enabled = 1;
        socket_selector_add(&mw->selector, mw->tcp_tnc2_server.listen_fd, SELECT_READ);
        LOG("tcp tnc2 server enabled on port %d", opts->tcp_tnc2_port);
    }

    // UDP senders
    mw->udp_kiss_enabled = 0;
    if (opts->udp_kiss_addr[0] && opts->udp_kiss_port > 0 && !udp_sender_init(&mw->udp_kiss_sender, opts->udp_kiss_addr, opts->udp_kiss_port))
    {
        mw->udp_kiss_enabled = 1;
        LOG("udp kiss sender enabled to %s:%d", opts->udp_kiss_addr, opts->udp_kiss_port);
    }

    mw->udp_tnc2_enabled = 0;
    if (opts->udp_tnc2_addr[0] && opts->udp_tnc2_port > 0 && !udp_sender_init(&mw->udp_tnc2_sender, opts->udp_tnc2_addr, opts->udp_tnc2_port))
    {
        mw->udp_tnc2_enabled = 1;
        LOG("udp tnc2 sender enabled to %s:%d", opts->udp_tnc2_addr, opts->udp_tnc2_port);
    }

    // UDP servers
    mw->udp_kiss_listen_enabled = 0;
    if (opts->udp_kiss_listen_port > 0 && !udp_server_init(&mw->udp_kiss_server, opts->udp_kiss_listen_port, 0))
    {
        mw->udp_kiss_listen_enabled = 1;
        socket_selector_add(&mw->selector, mw->udp_kiss_server.fd, SELECT_READ);
        LOG("udp kiss server enabled on port %d", opts->udp_kiss_listen_port);
    }

    mw->udp_tnc2_listen_enabled = 0;
    if (opts->udp_tnc2_listen_port > 0 && !udp_server_init(&mw->udp_tnc2_server, opts->udp_tnc2_listen_port, 0))
    {
        mw->udp_tnc2_listen_enabled = 1;
        socket_selector_add(&mw->selector, mw->udp_tnc2_server.fd, SELECT_READ);
        LOG("udp tnc2 server enabled on port %d", opts->udp_tnc2_listen_port);
    }

    // UDS KISS server (timeout 0 - non-blocking, selector handles waiting)
    mw->uds_kiss_enabled = 0;
    if (opts->uds_kiss_socket_path[0] && !uds_server_init(&mw->uds_kiss_server, opts->uds_kiss_socket_path, 0))
    {
        mw->uds_kiss_enabled = 1;
        socket_selector_add(&mw->selector, mw->uds_kiss_server.listen_fd, SELECT_READ);
        LOG("uds kiss server enabled on %s", opts->uds_kiss_socket_path);
    }

    // UDS TNC2 server
    mw->uds_tnc2_enabled = 0;
    if (opts->uds_tnc2_socket_path[0] && !uds_server_init(&mw->uds_tnc2_server, opts->uds_tnc2_socket_path, 0))
    {
        mw->uds_tnc2_enabled = 1;
        socket_selector_add(&mw->selector, mw->uds_tnc2_server.listen_fd, SELECT_READ);
        LOG("uds tnc2 server enabled on %s", opts->uds_tnc2_socket_path);
    }

    // Make stdin non-blocking and add to selector
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    socket_selector_add(&mw->selector, 0, SELECT_READ);

    if (opts->gain_2200 != 0.0f)
    {
        LOGV("enabling high boost filter with gain of %f dB", opts->gain_2200);
        bf_hbf_init(&mw->hbf_filter, 4, 2200.0f, sample_rate, opts->gain_2200);
    }

    modem_params_t modem_params = {
        .sample_rate = sample_rate,
        .types = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE,
        .tx_delay = opts->tx_delay,
        .tx_tail = opts->tx_tail};
    modem_init(&mw->modem, &modem_params);

    agc_init(&mw->input_agc, 10.0, 60e3f, sample_rate);

    sql_params_t sql_params = {
        .sample_rate = sample_rate,
        .init_threshold = 0.045f,
        .strength = 0.51f};
    sql_init(&mw->squelch, &sql_params, &sql_params_default);

    kiss_decoder_init(&mw->kiss_decoder);
    line_reader_init(&mw->stdin_line_reader, tnc2_input_callback);
    line_reader_init(&mw->tcp_line_reader, tnc2_input_callback);
    line_reader_init(&mw->udp_line_reader, tnc2_input_callback);
    line_reader_init(&mw->uds_line_reader, tnc2_input_callback);
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

    if (mw->uds_kiss_enabled)
        uds_server_free(&mw->uds_kiss_server);
    if (mw->uds_tnc2_enabled)
        uds_server_free(&mw->uds_tnc2_server);

    modem_free(&mw->modem);
    bf_biquad_free(&mw->hbf_filter);
}
