#include "options.h"
#include <limits.h>

void opts_init(options_t *opts)
{
    nonnull(opts, "opts");

    opts->config_file[0] = '\0';

    opts->list = false;
    opts->noop = false;
    opts->kiss = false;
    opts->log_level = LOG_LEVEL_STANDARD;

    opts->dev_name[0] = '\0';
    opts->dev_input = false;
    opts->dev_output = false;
    opts->rate = 0;

    opts->tcp_kiss_port = 0;
    opts->tcp_tnc2_port = 0;

    opts->udp_kiss_addr[0] = '\0';
    opts->udp_kiss_port = 0;
    opts->udp_tnc2_addr[0] = '\0';
    opts->udp_tnc2_port = 0;

    opts->udp_kiss_listen_port = 0;
    opts->udp_tnc2_listen_port = 0;

    opts->squelch = false;
    opts->gain_2200 = 0.0f;
    opts->tx_delay = 0.0f;
    opts->tx_tail = 0.0f;
    opts->exit_idle_s = 0;
}

void opts_defaults(options_t *opts)
{
    nonnull(opts, "opts");

    REPLACE_IF_a_WITH_b(opts->rate, 0, 44100);
    REPLACE_IF_a_WITH_b(opts->tx_delay, 0.0f, 300.0f);
    REPLACE_IF_a_WITH_b(opts->tx_tail, 0.0f, 30.0f);
    REPLACE_IF_a_WITH_b(opts->exit_idle_s, 0, LONG_MAX);
}
