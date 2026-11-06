#pragma once

#include "bitclk.h"
#include "buffer.h"
#include "demod.h"
#include "hldc.h"
#include "mod.h"
#include <stdint.h>
#include <time.h>

#define MD_RX_MAX 6
#define MD_QUAL_RING 64

// Per-demodulator loss-attribution counters (see tools/loss_decomp.py)
typedef struct md_demod_stats
{
    uint64_t samples;   // samples fed to the demodulator
    uint64_t bits;      // hard bits emitted by the bit clock
    uint64_t dcd_on;    // samples with DCD locked
    uint64_t frames_ok; // CRC-valid frames completed by the deframer
    uint64_t wins;      // frames this demod delivered to the output
    uint32_t last_frame_quality; // avg bitclk signal_quality over the last
                                 // completed frame's bits, x256 (0..32)
} md_demod_stats_t;

struct md_rx
{
    demod_t demod;
    bitclk_t bit_detector;
    hldc_deframer_t deframer;
    md_demod_stats_t stats;
    // rolling per-bit signal_quality backing last_frame_quality
    uint32_t qual_sum;
    uint16_t qual_count;
    uint8_t qual_pos;
    uint8_t qual_ring[MD_QUAL_RING];
};

struct md_multi_rx
{
    struct md_rx rxs[MD_RX_MAX];
    int count;

    // Inter-md_rx deduplication
    int last_modem;
    uint16_t last_crc;
    time_t last_time;
    uint64_t dedup_suppressed;
    uint32_t last_win_quality; // last_frame_quality of the demod that won last
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
    struct md_multi_rx *dedup_target;
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

// Points self-demodulation suppression at an external RX chain's
// md_multi_rx (defaults to modem's own unused mrx)
void modem_set_dedup_target(modem_t *modem, struct md_multi_rx *mrx);

int modem_demodulate(modem_t *modem, const float_buffer_t *sample_buf, buffer_t *out_frame_buf);

int modem_modulate(modem_t *modem, const buffer_t *frame_buf, float_buffer_t *out_sample_buf);

void modem_free(modem_t *modem);

//

void md_multi_rx_init(struct md_multi_rx *mrx, float sample_rate, demod_type_t types);

int md_multi_rx_process(
    struct md_multi_rx *mrx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf);

// Variant of md_multi_rx_process for non-real-time processing (i.e. simulation)
int md_multi_rx_process_at(
    struct md_multi_rx *mrx,
    const float_buffer_t *sample_buf,
    buffer_t *out_frame_buf,
    time_t time);

void md_multi_rx_free(struct md_multi_rx *mrx);

// One LOG line: per-demod counters and dedup suppressions
void md_stats_dump(const struct md_multi_rx *mrx, const char *tag);

//

void md_rx_init(struct md_rx *rx, float sample_rate, demod_type_t type);

int md_rx_process(
    struct md_rx *rx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf, uint16_t *out_crc);

void md_rx_free(struct md_rx *rx);

//

void md_tx_init(struct md_tx *tx, float sample_rate, float tx_delay, float tx_tail);

int md_tx_process(
    struct md_tx *tx, const buffer_t *frame_buf, float_buffer_t *out_sample_buf, uint16_t *out_crc);

void md_tx_free(struct md_tx *tx);
