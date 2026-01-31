#include "options.h"
#include "common.h"
#include "conf.h"
#include <string.h>

void opts_parse_conf_file(options_t *opts, const char *filename)
{
    nonnull(opts, "opts");
    if (NULL == filename || filename[0] == '\0')
        return;

    conf_t conf;
    conf_error_e err = conf_load(&conf, filename);
    EXITIF(err != CONF_SUCCESS, -1, "failed to load config file: %s (error %d)", filename, err);

    opts->list = conf_get_bool_or_default(&conf, OPT_LIST, opts->list);
    opts->noop = conf_get_bool_or_default(&conf, OPT_NOOP, opts->noop);
    opts->kiss = conf_get_bool_or_default(&conf, OPT_KISS, opts->kiss);
    opts->dev_input = conf_get_bool_or_default(&conf, OPT_DEV_INPUT, opts->dev_input);
    opts->dev_output = conf_get_bool_or_default(&conf, OPT_DEV_OUTPUT, opts->dev_output);

    opts->rate = conf_get_int_or_default(&conf, OPT_RATE, opts->rate);
    opts->tcp_kiss_port = conf_get_int_or_default(&conf, OPT_TCP_KISS_PORT, opts->tcp_kiss_port);
    opts->tcp_tnc2_port = conf_get_int_or_default(&conf, OPT_TCP_TNC2_PORT, opts->tcp_tnc2_port);
    opts->udp_kiss_port = conf_get_int_or_default(&conf, OPT_UDP_KISS_PORT, opts->udp_kiss_port);
    opts->udp_tnc2_port = conf_get_int_or_default(&conf, OPT_UDP_TNC2_PORT, opts->udp_tnc2_port);
    opts->udp_kiss_listen_port = conf_get_int_or_default(&conf, OPT_UDP_KISS_LISTEN_PORT, opts->udp_kiss_listen_port);
    opts->udp_tnc2_listen_port = conf_get_int_or_default(&conf, OPT_UDP_TNC2_LISTEN_PORT, opts->udp_tnc2_listen_port);

    opts->squelch = conf_get_float_or_default(&conf, OPT_SQUELCH, opts->squelch);
    opts->gain_2200 = conf_get_float_or_default(&conf, OPT_GAIN_2200, opts->gain_2200);
    opts->tx_delay = conf_get_float_or_default(&conf, OPT_TX_DELAY, opts->tx_delay);
    opts->tx_tail = conf_get_float_or_default(&conf, OPT_TX_TAIL, opts->tx_tail);

    const char *val;
    val = conf_get_str_or_default(&conf, OPT_DEV_NAME, opts->dev_name);
    if (opts->dev_name[0] == '\0')
        strncpy(opts->dev_name, val, OPT_STR_SIZE - 1);
    val = conf_get_str_or_default(&conf, OPT_UDP_KISS_ADDR, opts->udp_kiss_addr);
    if (opts->udp_kiss_addr[0] == '\0')
        strncpy(opts->udp_kiss_addr, val, OPT_STR_SIZE - 1);
    val = conf_get_str_or_default(&conf, OPT_UDP_TNC2_ADDR, opts->udp_tnc2_addr);
    if (opts->udp_tnc2_addr[0] == '\0')
        strncpy(opts->udp_tnc2_addr, val, OPT_STR_SIZE - 1);

    val = conf_get_str_or_default(&conf, OPT_UDS_KISS_SOCKET, opts->uds_kiss_socket_path);
    if (opts->uds_kiss_socket_path[0] == '\0')
        strncpy(opts->uds_kiss_socket_path, val, OPT_STR_SIZE - 1);

    val = conf_get_str_or_default(&conf, OPT_UDS_TNC2_SOCKET, opts->uds_tnc2_socket_path);
    if (opts->uds_tnc2_socket_path[0] == '\0')
        strncpy(opts->uds_tnc2_socket_path, val, OPT_STR_SIZE - 1);
}
