#pragma once

#include "modem.h"
#include "filter.h"
#include "agc.h"
#include "squelch.h"
#include <time.h>

// The one RX chain shared by every miniwolf front end:
// hbf equalization -> AGC -> squelch (compacting) -> multi-demodulator.
typedef struct rx_chain
{
    bf_biquad_t hbf;
    agc_t agc;
    sql_t squelch;
    struct md_multi_rx mrx;
    int use_squelch;
} rx_chain_t;

typedef struct rx_params
{
    float sample_rate;
    float gain_2200;        // extra dB at 2200 Hz (0 = identity)
    int use_squelch;        // squelch gating; closed samples are dropped
    float squelch_strength; // when use_squelch
    demod_type_t types;
} rx_params_t;

void rx_chain_init(rx_chain_t *rx, const rx_params_t *params);

// Pushes n samples through the chain with in-place squelch compaction and
// writes any demodulated frame to out_frame_buf. Returns the frame length
// (0 = none, negative = error). `now` feeds the inter-demodulator dedup;
// live callers pass time(NULL), offline callers their synthetic clock.
int rx_chain_process(rx_chain_t *rx, float *samples, int n, time_t now,
                     buffer_t *out_frame_buf);

void rx_chain_free(rx_chain_t *rx);
