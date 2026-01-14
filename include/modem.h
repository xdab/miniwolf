#pragma once

#include "mod.h"
#include "hldc.h"
#include "demod.h"
#include "bitclk.h"
#include "buffer.h"
#include "dedupe.h"

#define MD_RX_MAX 6

struct md_rx
{
    demod_t demod;
    bitclk_t bit_detector_simple;
    bitclk2_t bit_detector_pll;
    hldc_deframer_t deframer_simple;
    hldc_deframer_t deframer_pll;
    dedupe_t deframer_dedupe;
};

struct md_multi_rx
{
    struct md_rx rxs[MD_RX_MAX];
    int count;
    dedupe_t multi_rx_dedupe;
};

struct md_tx
{
    modulator_t fsk_mod;
    hldc_framer_t framer;
};

typedef struct modem
{
    struct md_multi_rx mrx;
    struct md_tx tx;
} modem_t;

typedef struct modem_params
{
    float sample_rate;
    demod_type_t types;
    float tx_delay;
    float tx_tail;
} modem_params_t;

//

void modem_init(modem_t *modem, modem_params_t *params);

int modem_demodulate(modem_t *modem, const float_buffer_t *sample_buf, buffer_t *out_frame_buf);

int modem_modulate(modem_t *modem, const buffer_t *frame_buf, float_buffer_t *out_sample_buf);

void modem_free(modem_t *modem);

//

void md_multi_rx_init(struct md_multi_rx *mrx, float sample_rate, demod_type_t types);

int md_multi_rx_process(struct md_multi_rx *mrx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf);

void md_multi_rx_free(struct md_multi_rx *mrx);

//

void md_rx_init(struct md_rx *rx, float sample_rate, uint32_t demod_flags);

int md_rx_process(struct md_rx *rx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf, uint16_t *out_crc);

void md_rx_free(struct md_rx *rx);

//

void md_tx_init(struct md_tx *tx, float sample_rate, float tx_delay, float tx_tail);

int md_tx_process(struct md_tx *tx, const buffer_t *frame_buf, float_buffer_t *out_sample_buf, uint16_t *out_crc);

void md_tx_free(struct md_tx *tx);
