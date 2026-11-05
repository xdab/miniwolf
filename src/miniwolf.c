#include "miniwolf.h"
#include "common.h"
#include "demod.h"
#include "device.h"
#include "kiss.h"
#include "line.h"
#include "loop.h"
#include "modem.h"
#include "options.h"
#include "poller.h"
#include "rx.h"
#include "tcp.h"
#include "udp.h"
#include "uds.h"
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

miniwolf_t g_miniwolf;

void miniwolf_init(miniwolf_t *mw, const options_t *opts)
{
    nonnull(mw, "mw");
    nonnull(opts, "opts");

    mw->max_idle_time = opts->exit_idle_s;
    mw->last_packet_time = time(NULL);

    mw->squelch_enabled = opts->squelch > 0.0f;
    mw->kiss_mode = opts->kiss;
    mw->tnc2_extras = opts->tnc2_extras;
    float sample_rate = (float)opts->rate;

    socket_poller_init(&mw->poller);

    // TCP servers
    mw->tcp_kiss_enabled = 0;
    if (opts->tcp_kiss_port > 0 && !tcp_server_init(&mw->tcp_kiss_server, opts->tcp_kiss_port, 0))
    {
        mw->tcp_kiss_enabled = 1;
        mw->tcp_kiss_server.on_client_connect = tcp_client_connect_cb;
        mw->tcp_kiss_server.on_client_disconnect = tcp_client_disconnect_cb;
        mw->tcp_kiss_server.user_data = mw;
        socket_poller_add(&mw->poller, mw->tcp_kiss_server.listen_fd, POLLER_EV_IN);
        LOG("tcp kiss server enabled on port %d", opts->tcp_kiss_port);
    }

    mw->tcp_tnc2_enabled = 0;
    if (opts->tcp_tnc2_port > 0 && !tcp_server_init(&mw->tcp_tnc2_server, opts->tcp_tnc2_port, 0))
    {
        mw->tcp_tnc2_enabled = 1;
        mw->tcp_tnc2_server.on_client_connect = tcp_client_connect_cb;
        mw->tcp_tnc2_server.on_client_disconnect = tcp_client_disconnect_cb;
        mw->tcp_tnc2_server.user_data = mw;
        socket_poller_add(&mw->poller, mw->tcp_tnc2_server.listen_fd, POLLER_EV_IN);
        LOG("tcp tnc2 server enabled on port %d", opts->tcp_tnc2_port);
    }

    // UDP senders
    mw->udp_kiss_enabled = 0;
    if (opts->udp_kiss_addr[0] && opts->udp_kiss_port > 0 &&
        !udp_sender_init(&mw->udp_kiss_sender, opts->udp_kiss_addr, opts->udp_kiss_port))
    {
        mw->udp_kiss_enabled = 1;
        LOG("udp kiss sender enabled to %s:%d", opts->udp_kiss_addr, opts->udp_kiss_port);
    }

    mw->udp_tnc2_enabled = 0;
    if (opts->udp_tnc2_addr[0] && opts->udp_tnc2_port > 0 &&
        !udp_sender_init(&mw->udp_tnc2_sender, opts->udp_tnc2_addr, opts->udp_tnc2_port))
    {
        mw->udp_tnc2_enabled = 1;
        LOG("udp tnc2 sender enabled to %s:%d", opts->udp_tnc2_addr, opts->udp_tnc2_port);
    }

    // UDP servers
    mw->udp_kiss_listen_enabled = 0;
    if (opts->udp_kiss_listen_port > 0 &&
        !udp_server_init(&mw->udp_kiss_server, opts->udp_kiss_listen_port, 0))
    {
        mw->udp_kiss_listen_enabled = 1;
        socket_poller_add(&mw->poller, mw->udp_kiss_server.fd, POLLER_EV_IN);
        LOG("udp kiss server enabled on port %d", opts->udp_kiss_listen_port);
    }

    mw->udp_tnc2_listen_enabled = 0;
    if (opts->udp_tnc2_listen_port > 0 &&
        !udp_server_init(&mw->udp_tnc2_server, opts->udp_tnc2_listen_port, 0))
    {
        mw->udp_tnc2_listen_enabled = 1;
        socket_poller_add(&mw->poller, mw->udp_tnc2_server.fd, POLLER_EV_IN);
        LOG("udp tnc2 server enabled on port %d", opts->udp_tnc2_listen_port);
    }

    // UDS KISS server (timeout 0 - non-blocking, selector handles waiting)
    mw->uds_kiss_enabled = 0;
    if (opts->uds_kiss_socket_path[0] &&
        !uds_server_init(&mw->uds_kiss_server, opts->uds_kiss_socket_path, 0))
    {
        mw->uds_kiss_enabled = 1;
        mw->uds_kiss_server.on_client_connect = uds_client_connect_cb;
        mw->uds_kiss_server.on_client_disconnect = uds_client_disconnect_cb;
        mw->uds_kiss_server.user_data = mw;
        socket_poller_add(&mw->poller, mw->uds_kiss_server.listen_fd, POLLER_EV_IN);
        LOG("uds kiss server enabled on %s", opts->uds_kiss_socket_path);
    }

    // UDS TNC2 server
    mw->uds_tnc2_enabled = 0;
    if (opts->uds_tnc2_socket_path[0] &&
        !uds_server_init(&mw->uds_tnc2_server, opts->uds_tnc2_socket_path, 0))
    {
        mw->uds_tnc2_enabled = 1;
        mw->uds_tnc2_server.on_client_connect = uds_client_connect_cb;
        mw->uds_tnc2_server.on_client_disconnect = uds_client_disconnect_cb;
        mw->uds_tnc2_server.user_data = mw;
        socket_poller_add(&mw->poller, mw->uds_tnc2_server.listen_fd, POLLER_EV_IN);
        LOG("uds tnc2 server enabled on %s", opts->uds_tnc2_socket_path);
    }

    // Make stdin non-blocking and add to selector
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    socket_poller_add(&mw->poller, 0, POLLER_EV_IN);

    if (opts->gain_2200 != 0.0f)
        LOGV("enabling high boost filter with gain of %f dB", opts->gain_2200);

    rx_params_t rx_params = {.sample_rate = sample_rate,
                             .gain_2200 = opts->gain_2200,
                             .use_squelch = opts->squelch > 0.0f,
                             .squelch_strength = opts->squelch,
                             .types = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE};
    rx_chain_init(&mw->rx, &rx_params);

    mw->device = device_open_alsa();
    EXITIF(!mw->device, EXIT_FAILURE, "failed to open capture device");

    // Add audio capture fd to poller
    mw->audio_fd = device_poll_fd(mw->device);
    nonzero(mw->audio_fd, "audio_fd");
    if (socket_poller_add(&mw->poller, mw->audio_fd, POLLER_EV_IN) < 0)
        EXIT("failed to add audio fd to poller");

    modem_params_t modem_params = {.sample_rate = sample_rate,
                                   .types = DEMOD_ALL_GOERTZEL | DEMOD_QUADRATURE,
                                   .tx_delay = opts->tx_delay,
                                   .tx_tail = opts->tx_tail};
    modem_init(&mw->modem, &modem_params);
    // Self-demodulation suppression must seed the RX chain, not modem's own mrx
    modem_set_dedup_target(&mw->modem, &mw->rx.mrx);

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
    rx_chain_free(&mw->rx);
    if (mw->device)
        device_free(mw->device);
    socket_poller_free(&mw->poller);
}
