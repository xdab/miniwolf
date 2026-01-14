#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <argp.h>
#include <time.h>
#include <string.h>
#include "common.h"
#include "tcp.h"
#include "udp.h"
#include "kiss.h"
#include "tnc2.h"
#include "line.h"
#include "ax25.h"
#include "dedupe.h"
#include "crc.h"

typedef struct log_args
{
    char *tcp_addr;
    int tcp_port;
    int udp_port;
    char *prefix;
    bool kiss;
    log_level_e log_level;
} log_args_t;

static struct argp_option log_options[] = {
    {"tcp-addr", '\x01', "ADDR", 0, "TCP server address", 1},
    {"tcp-port", '\x02', "PORT", 0, "TCP server port", 1},
    {"udp-port", '\x03', "PORT", 0, "UDP listening port", 1},
    {"prefix", 'p', "STR", 0, "Log file prefix (default: \"rx\")", 2},
    {"kiss", 'k', 0, 0, "Use KISS protocol instead of TNC2", 2},
    {"verbose", 'v', 0, 0, "Print verbose logs", 3},
    {"debug", 'V', 0, 0, "Print verbose and debugging logs", 3},
    {0, 0, 0, 0, 0, 0}};

static error_t log_parse_opt(int key, char *arg, struct argp_state *state)
{
    log_args_t *args = state->input;
    switch (key)
    {
    case '\x01':
        args->tcp_addr = arg;
        break;
    case '\x02':
        args->tcp_port = atoi(arg);
        break;
    case '\x03':
        args->udp_port = atoi(arg);
        break;
    case 'p':
        args->prefix = arg;
        break;
    case 'k':
        args->kiss = true;
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

static struct argp log_argp = {
    log_options,
    log_parse_opt,
    "",
    "Logger for AX.25 RF traffic"};

static void logs_args_parse(int argc, char *argv[], log_args_t *args)
{
    args->tcp_addr = NULL;
    args->kiss = false;
    args->log_level = LOG_LEVEL_STANDARD;
    args->prefix = "rx";

    argp_parse(&log_argp, argc, argv, 0, 0, args);
}

void process_input(const char *buf, int len);

void line_callback(const buffer_t *line_buf);
void kiss_callback(const kiss_message_t *kiss_msg);
void packet_callback(const ax25_packet_t *packet);

static void write_to_log(const char *prefix, time_t timestamp, const char *data, int len)
{
    nonnull(prefix, "prefix");
    nonnull(data, "data");

    struct tm *tm_info = localtime(&timestamp);
    char date_str[9] = {0};
    strftime(date_str, sizeof(date_str), "%Y%m%d", tm_info);

    char file_name[256];
    if (snprintf(file_name, sizeof(file_name), "%s-%s.log", prefix, date_str) >= (int)sizeof(file_name))
    {
        LOG("filename too long");
        return;
    }

    FILE *log_file = fopen(file_name, "a");
    if (!log_file)
        EXIT("failed to open log file: %s", file_name);

    fprintf(log_file, "%d\t%.*s\r\n", (int)timestamp, len, data);
    fclose(log_file);
}

static bool kiss_format = false;
static bool udp_mode = false;
static udp_server_t udp_server;
static tcp_client_t tcp_client;
static line_reader_t line_reader;
static kiss_decoder_t kiss_decoder;
static dedupe_t dedupe;
static const char *log_file_prefix;

int main(int argc, char *argv[])
{
    int exit_code = EXIT_SUCCESS;
    log_args_t args = {0};
    logs_args_parse(argc, argv, &args);

    _log_level = args.log_level;
    kiss_format = args.kiss;
    log_file_prefix = args.prefix;

    dedupe_init(&dedupe, 4);

    if (kiss_format)
        kiss_decoder_init(&kiss_decoder);
    else
        line_reader_init(&line_reader, line_callback);

    if (args.tcp_addr != NULL && args.tcp_port > 0)
    {
        LOGV("tcp client mode %s:%d", args.tcp_addr, args.tcp_port);
        if (tcp_client_init(&tcp_client, args.tcp_addr, args.tcp_port))
            EXIT("failed to connect to TCP server");
    }
    else if (args.udp_port > 0)
    {
        udp_mode = true;
        LOGV("udp client mode :%d", args.udp_port);

        if (udp_server_init(&udp_server, args.udp_port))
            EXIT("failed to start UDP listener");
    }
    else
        EXIT("incomplete arguments");

    for (;;)
    {
        static char read_buffer[TCP_READ_BUF_SIZE];
        if (udp_mode)
        {
            int n = udp_server_process(&udp_server, read_buffer, sizeof(read_buffer));
            process_input(read_buffer, n);
        }
        else
        {
            int n = tcp_client_process(&tcp_client, read_buffer, sizeof(read_buffer));
            if (n > 0)
                process_input(read_buffer, n);
            else if (n < 0)
            {
                LOG("TCP connection lost, exiting");
                exit_code = EXIT_FAILURE;
                goto EXIT;
            }
        }
    }

EXIT:
    if (udp_mode)
        udp_server_free(&udp_server);
    else
        tcp_client_free(&tcp_client);

    return exit_code;
}

void process_input(const char *buf, int len)
{
    if (kiss_format)
    {
        kiss_message_t kiss_msg;
        for (int i = 0; i < len; ++i)
            if (kiss_decoder_process(&kiss_decoder, buf[i], &kiss_msg))
                kiss_callback(&kiss_msg);
    }
    else
    {
        for (int i = 0; i < len; ++i)
            line_reader_process(&line_reader, buf[i]);
    }
}

void line_callback(const buffer_t *line_buf)
{
    assert_buffer_valid(line_buf);

    ax25_packet_t packet;
    if (tnc2_string_to_packet(&packet, line_buf))
    {
        LOG("error parsing line as tnc2 packet string");
        return;
    }

    packet_callback(&packet);
}

void kiss_callback(const kiss_message_t *kiss_msg)
{
    nonnull(kiss_msg, "kiss_msg");

    if (kiss_msg->command != 0 || kiss_msg->port != 0)
        return;

    buffer_t frame_buf = {
        .data = (unsigned char *)kiss_msg->data,
        .capacity = sizeof(kiss_msg->data),
        .size = kiss_msg->data_length};

    ax25_packet_t packet;
    if (ax25_packet_unpack(&packet, &frame_buf))
    {
        LOG("error unpacking KISS frame data to a packet");
        return;
    }

    packet_callback(&packet);
}

void packet_callback(const ax25_packet_t *packet)
{
    nonnull(packet, "packet");
    time_t now = time(NULL);

    char data[512];
    buffer_t buffer = {
        .data = data,
        .capacity = sizeof(data),
        .size = 0};

    if (ax25_packet_pack(packet, &buffer))
    {
        LOG("error packing packet");
        return;
    }

    crc_ccitt_t crc;
    crc_ccitt_init(&crc);
    crc_ccitt_update_buffer(&crc, buffer.data, buffer.size);
    uint16_t frame_crc = crc_ccitt_get(&crc);

    if (dedupe_push_frame(&dedupe, frame_crc))
    {
        LOGD("duplicate frame");
        return;
    }

    if (!tnc2_packet_to_string(packet, &buffer))
    {
        LOG("error converting packet to tnc2 string");
        return;
    }

    LOGV("%d %.*s", (int)now, buffer.size, buffer.data);
    write_to_log(log_file_prefix, now, buffer.data, buffer.size);
}
