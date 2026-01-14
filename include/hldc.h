#pragma once

#include "common.h"
#include "buffer.h"
#include <stdint.h>

typedef enum
{
    HLDC_SUCCESS = 0,
    HLDC_FRAME_TOO_SMALL,
    HLDC_INVALID_FCS,
    HLDC_BUF_TOO_SMALL,
    HLDC_OTHER
} hldc_error_e;

typedef struct hldc_framer
{
    int head_flags;
    int tail_flags;
    int nrzi_bit;
    int ones_count;
} hldc_framer_t;

void hldc_framer_init(hldc_framer_t *framer, int head_flags, int tail_flags);

hldc_error_e hldc_framer_process(hldc_framer_t *framer, const buffer_t *frame_buf, buffer_t *out_bits_buf, uint16_t *out_crc);

typedef struct hldc_deframer
{
    uint8_t bytes[512];
    int bytes_len;
    uint8_t raw_bits;
    uint8_t unstuffed_bits;
    int unstuffed_bit_count;
    int ones_count;
    int last_bit;
    int min_frame_size;
} hldc_deframer_t;

void hldc_deframer_init(hldc_deframer_t *deframer);

hldc_error_e hldc_deframer_process(hldc_deframer_t *deframer, int bit, buffer_t *out_frame_buf, uint16_t *out_crc);
