#include "rx.h"

#include "agc.h"
#include "buffer.h"
#include "common.h"
#include "filter.h"
#include "modem.h"
#include "squelch.h"
#include <string.h>
#include <time.h>

void rx_chain_init(rx_chain_t *rx, const rx_params_t *params)
{
    nonnull(rx, "rx");
    nonnull(params, "params");
    nonzero(params->sample_rate, "params.sample_rate");

    memset(rx, 0, sizeof(*rx));

    bf_hbf_init(&rx->hbf, 4, 2200.0f, params->sample_rate, params->gain_2200);
    agc_init(&rx->agc, 10.0f, 60e3f, params->sample_rate);

    rx->use_squelch = params->use_squelch;
    if (rx->use_squelch)
    {
        sql_params_t sql_params = {.sample_rate = params->sample_rate,
                                   .init_threshold = 0.045f,
                                   .strength = params->squelch_strength};
        sql_init(&rx->squelch, &sql_params, &sql_params_default);
    }

    md_multi_rx_init(&rx->mrx, params->sample_rate, params->types);
}

// n/now both pass small ints; a swap is caught by the caller-side frame check
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
int rx_chain_process(rx_chain_t *rx, float *samples, int n, time_t now, buffer_t *out_frame_buf)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    nonnull(rx, "rx");
    nonnull(samples, "samples");
    assert_buffer_valid(out_frame_buf);

    int kept = 0;
    for (int i = 0; i < n; i++)
    {
        float s = bf_biquad_filter(&rx->hbf, samples[i]);
        s = agc_filter(&rx->agc, s);
        if (rx->use_squelch && !sql_process(&rx->squelch, s))
            continue;
        samples[kept++] = s;
    }

    if (kept == 0)
        return 0;

    float_buffer_t sample_buf = {.data = samples, .capacity = n, .size = kept};

    return md_multi_rx_process_at(&rx->mrx, &sample_buf, out_frame_buf, now);
}

void rx_chain_free(rx_chain_t *rx)
{
    nonnull(rx, "rx");
    bf_biquad_free(&rx->hbf);
    md_multi_rx_free(&rx->mrx);
}
