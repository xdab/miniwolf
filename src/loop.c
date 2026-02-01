#include "loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include "audio.h"
#include "ax25.h"
#include "tnc2.h"
#include "common.h"

#define INPUT_CALLBACK_SIZE 4096
#define STDIN_BUFFER_SIZE 2048
#define MAX_POLL_FDS 32

int audio_input_callback(float_buffer_t *buf);
void modulate_and_transmit(const buffer_t *frame_buf);

void tnc2_input_callback(const buffer_t *line_buf);
void kiss_input_callback(kiss_message_t *kiss_msg);

void process_stdin_input(miniwolf_t *mw);
void process_tcp_input(miniwolf_t *mw);
void process_udp_input(miniwolf_t *mw);
void process_uds_input(miniwolf_t *mw);

void modulate_and_transmit(const buffer_t *frame_buf)
{
    static float sample_data[96000];
    float_buffer_t sample_buf = {
        .data = sample_data,
        .capacity = sizeof(sample_data) / sizeof(float),
        .size = 0};
    modem_modulate(&g_miniwolf.modem, frame_buf, &sample_buf);
    aud_output(&sample_buf);
}

void loop_run(miniwolf_t *mw)
{
    static float audio_input_buffer[INPUT_CALLBACK_SIZE];
    static float_buffer_t audio_buf = {
        .data = audio_input_buffer,
        .capacity = INPUT_CALLBACK_SIZE,
        .size = 0};

    struct pollfd pfds[MAX_POLL_FDS];
    int nfds = 0;

    for (;;)
    {
        nfds = 0;

        // Add ALSA capture poll descriptors
        int audio_fd_count = aud_get_capture_poll_fds(&pfds[nfds], MAX_POLL_FDS - nfds);
        if (audio_fd_count < 0)
        {
            LOG("failed to get audio poll fds");
            return;
        }
        int audio_fd_start = nfds;
        nfds += audio_fd_count;

        int ret = poll(pfds, nfds, 10);

        if (ret < 0)
        {
            if (errno == EINTR)
            {
                LOGD("poll interrupted by signal");
                continue;
            }
            LOG("poll error: %s", strerror(errno));
            return;
        }

        // Process audio capture if ready
        if (audio_fd_count > 0)
            aud_process_capture_events(&pfds[audio_fd_start], audio_fd_count, audio_input_callback, &audio_buf);

        // Always process playback (non-blocking)
        aud_process_playback();

        // Wait for socket activity using selector
        int sel_ret = socket_selector_wait(&mw->selector, 100);
        if (sel_ret > 0 && socket_selector_is_ready(&mw->selector, 0))
            process_stdin_input(mw);

        if (mw->tcp_kiss_enabled)
            process_tcp_input(mw);

        if (mw->udp_kiss_listen_enabled)
            process_udp_input(mw);

        if (mw->uds_kiss_enabled)
            process_uds_input(mw);

        // Check exit-idle condition
        time_t current_time = time(NULL);
        EXITIF(current_time - mw->last_packet_time > mw->max_idle_time, EXIT_FAILURE, "Lack of activity");
    }
}

int audio_input_callback(float_buffer_t *buf)
{
    assert_buffer_valid(buf);

    // If configured, apply high boost channel equalization
    if (g_miniwolf.hbf_filter.n > 0)
    {
        for (int i = 0; i < buf->size; i++)
            buf->data[i] = bf_biquad_filter(&g_miniwolf.hbf_filter, buf->data[i]);
    }

    // If configured, apply squelch
    if (g_miniwolf.squelch_enabled)
    {
        int samples_passed = 0;
        for (int i = 0; i < buf->size; i++)
        {
            buf->data[i] = agc_filter(&g_miniwolf.input_agc, buf->data[i]);
            if (sql_process(&g_miniwolf.squelch, buf->data[i]))
                buf->data[samples_passed++] = buf->data[i];
        }
        buf->size = samples_passed;
    }

    if (buf->size == 0)
        return 0;

    char frame_buffer[512];
    buffer_t frame_buf = {
        .data = frame_buffer,
        .capacity = sizeof(frame_buffer),
        .size = 0};

    int frame_len = modem_demodulate(&g_miniwolf.modem, buf, &frame_buf);
    if (frame_len <= 0)
        return 0;

    g_miniwolf.last_packet_time = time(NULL);
    LOGV("demodulated packet: %d bytes", frame_len);

    if (g_miniwolf.kiss_mode || g_miniwolf.tcp_kiss_enabled)
    {
        kiss_message_t kiss_msg;
        kiss_msg.port = 0;
        kiss_msg.command = 0;
        memcpy(&kiss_msg.data, frame_buffer, frame_len);
        kiss_msg.data_length = frame_len;

        char kiss_buffer[512];
        int kiss_len = kiss_encode(&kiss_msg, kiss_buffer, sizeof(kiss_buffer));
        if (kiss_len <= 0)
            return 0;

        if (g_miniwolf.kiss_mode)
        {
            fwrite(kiss_buffer, 1, kiss_len, stdout);
            fflush(stdout);
        }

        buffer_t kiss_send_buf = {.data = (unsigned char *)kiss_buffer, .capacity = sizeof(kiss_buffer), .size = kiss_len};

        if (g_miniwolf.tcp_kiss_enabled)
            tcp_server_broadcast(&g_miniwolf.tcp_kiss_server, &kiss_send_buf);

        if (g_miniwolf.udp_kiss_enabled)
            udp_sender_send(&g_miniwolf.udp_kiss_sender, &kiss_send_buf);

        if (g_miniwolf.uds_kiss_enabled)
            uds_server_broadcast(&g_miniwolf.uds_kiss_server, &kiss_send_buf);
    }

    if (!g_miniwolf.kiss_mode || g_miniwolf.tcp_tnc2_enabled)
    {
        ax25_packet_t packet;
        if (ax25_packet_unpack(&packet, &frame_buf))
            return 0;

        char tnc2_data[512];
        buffer_t tnc2_buf = {
            .data = (unsigned char *)tnc2_data,
            .capacity = sizeof(tnc2_data),
            .size = 0};
        int tnc2_len = tnc2_packet_to_string(&packet, &tnc2_buf);
        if (tnc2_len <= 0)
            return 0;

        // Add newline before the end of the tnc2 string
        if (tnc2_len + 1 < sizeof(tnc2_data))
        {
            tnc2_data[tnc2_len++] = '\n';
            tnc2_data[tnc2_len] = '\0';
        }

        if (!g_miniwolf.kiss_mode)
        {
            fwrite(tnc2_data, 1, tnc2_len, stdout);
            fflush(stdout);
        }

        buffer_t tnc2_send_buf = {.data = (unsigned char *)tnc2_data, .capacity = sizeof(tnc2_data), .size = tnc2_len};

        if (g_miniwolf.tcp_tnc2_enabled)
            tcp_server_broadcast(&g_miniwolf.tcp_tnc2_server, &tnc2_send_buf);

        if (g_miniwolf.udp_tnc2_enabled)
            udp_sender_send(&g_miniwolf.udp_tnc2_sender, &tnc2_send_buf);

        if (g_miniwolf.uds_tnc2_enabled)
            uds_server_broadcast(&g_miniwolf.uds_tnc2_server, &tnc2_send_buf);
    }

    return 0;
}

void tnc2_input_callback(const buffer_t *line_buf)
{
    assert_buffer_valid(line_buf);

    ax25_packet_t packet;
    if (tnc2_string_to_packet(&packet, line_buf))
    {
        LOGV("error parsing line as tnc2 packet string");
        return;
    }

    char frame_data[AX25_MAX_PACKET_LEN];
    buffer_t frame_buf = {
        .data = frame_data,
        .capacity = sizeof(frame_data),
        .size = 0};
    if (ax25_packet_pack(&packet, &frame_buf))
    {
        LOGV("error packing ax25 packet");
        return;
    }

    modulate_and_transmit(&frame_buf);
}

void kiss_input_callback(kiss_message_t *kiss_msg)
{
    nonnull(kiss_msg, "kiss_msg");

    if (kiss_msg->command != 0 || kiss_msg->port != 0)
        return;

    if (kiss_msg->data_length > sizeof(kiss_msg->data))
    {
        LOG("invalid KISS data length: %d > %zu", kiss_msg->data_length, sizeof(kiss_msg->data));
        return;
    }

    buffer_t frame_buf = {
        .data = kiss_msg->data,
        .capacity = sizeof(kiss_msg->data),
        .size = kiss_msg->data_length};

    modulate_and_transmit(&frame_buf);
}

void process_stdin_input(miniwolf_t *mw)
{
    static unsigned char read_buffer[STDIN_BUFFER_SIZE];
    int n = read(0, read_buffer, sizeof(read_buffer));

    if (mw->kiss_mode)
    {
        kiss_message_t kiss_msg;
        for (int i = 0; i < n; ++i)
            if (kiss_decoder_process(&mw->kiss_decoder, read_buffer[i], &kiss_msg))
                kiss_input_callback(&kiss_msg);
    }
    else
    {
        for (int i = 0; i < n; ++i)
            line_reader_process(&mw->stdin_line_reader, read_buffer[i]);
    }
}

void process_tcp_input(miniwolf_t *mw)
{
    static unsigned char read_buffer[TCP_READ_BUF_SIZE];
    buffer_t buf = {
        .data = read_buffer,
        .capacity = TCP_READ_BUF_SIZE,
        .size = 0};

    if (mw->tcp_kiss_enabled)
    {
        kiss_message_t kiss_msg;
        int n = tcp_server_listen(&mw->tcp_kiss_server, &buf);
        for (int i = 0; i < n; ++i)
            if (kiss_decoder_process(&mw->kiss_decoder, read_buffer[i], &kiss_msg))
                kiss_input_callback(&kiss_msg);
    }

    if (mw->tcp_tnc2_enabled)
    {
        int n = tcp_server_listen(&mw->tcp_tnc2_server, &buf);
        for (int i = 0; i < n; ++i)
            line_reader_process(&mw->tcp_line_reader, read_buffer[i]);
    }
}

void process_udp_input(miniwolf_t *mw)
{
    static unsigned char read_buffer[TCP_READ_BUF_SIZE];
    buffer_t buf = {
        .data = read_buffer,
        .capacity = TCP_READ_BUF_SIZE,
        .size = 0};

    if (mw->udp_kiss_listen_enabled)
    {
        kiss_message_t kiss_msg;
        int n = udp_server_listen(&mw->udp_kiss_server, &buf);
        for (int i = 0; i < n; ++i)
            if (kiss_decoder_process(&mw->kiss_decoder, read_buffer[i], &kiss_msg))
                kiss_input_callback(&kiss_msg);
    }

    if (mw->udp_tnc2_listen_enabled)
    {
        int n = udp_server_listen(&mw->udp_tnc2_server, &buf);
        for (int i = 0; i < n; ++i)
            line_reader_process(&mw->udp_line_reader, read_buffer[i]);
    }
}

void process_uds_input(miniwolf_t *mw)
{
    static unsigned char read_buffer[UDS_READ_BUF_SIZE];
    buffer_t buf = {
        .data = read_buffer,
        .capacity = UDS_READ_BUF_SIZE,
        .size = 0};

    if (mw->uds_kiss_enabled)
    {
        kiss_message_t kiss_msg;
        int n = uds_server_listen(&mw->uds_kiss_server, &buf);
        for (int i = 0; i < n; ++i)
            if (kiss_decoder_process(&mw->kiss_decoder, read_buffer[i], &kiss_msg))
                kiss_input_callback(&kiss_msg);
    }

    if (mw->uds_tnc2_enabled)
    {
        int n = uds_server_listen(&mw->uds_tnc2_server, &buf);
        for (int i = 0; i < n; ++i)
            line_reader_process(&mw->uds_line_reader, read_buffer[i]);
    }
}
