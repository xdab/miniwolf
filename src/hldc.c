#include "hldc.h"
#include "crc.h"
#include "nrzi.h"
#include "common.h"
#include <string.h>

#define HLDC_FLAG 0x7E

static void hldc_framer_add_bit_unstuffed(buffer_t *out_bits_buf, int *nrzi_bit, int bit)
{
    bit = nrzi_encode(bit, nrzi_bit);
    out_bits_buf->data[out_bits_buf->size++] = bit;
}

static void hldc_framer_add_bit_stuffed(buffer_t *out_bits_buf, int *nrzi_bit, int *ones_count, int bit)
{
    bit = bit ? 1 : 0;
    hldc_framer_add_bit_unstuffed(out_bits_buf, nrzi_bit, bit);

    if (bit)
        (*ones_count)++;
    else
        *ones_count = 0;

    if (*ones_count == 5)
        hldc_framer_add_bit_stuffed(out_bits_buf, nrzi_bit, ones_count, 0);
}

static void hldc_framer_add_byte_unstuffed(hldc_framer_t *framer, buffer_t *out_bits_buf, uint8_t byte)
{
    for (int i = 0; i < 8; i++)
        hldc_framer_add_bit_unstuffed(out_bits_buf, &framer->nrzi_bit, byte & (1 << i));
}

static void hldc_framer_add_byte_stuffed(hldc_framer_t *framer, buffer_t *out_bits_buf, uint8_t byte)
{
    for (int i = 0; i < 8; i++)
        hldc_framer_add_bit_stuffed(out_bits_buf, &framer->nrzi_bit, &framer->ones_count, byte & (1 << i));
}

void hldc_framer_init(hldc_framer_t *framer, int head_flags, int tail_flags)
{
    nonnull(framer, "framer");

    framer->head_flags = head_flags;
    framer->tail_flags = tail_flags;
    framer->nrzi_bit = 0;
    framer->ones_count = 0;
}

hldc_error_e hldc_framer_process(hldc_framer_t *framer, const buffer_t *frame_buf, buffer_t *out_bits_buf, uint16_t *out_crc)
{
    nonnull(framer, "framer");
    assert_buffer_valid(frame_buf);
    assert_buffer_valid(out_bits_buf);

    out_bits_buf->size = 0;

    // Head flags
    for (int i = 0; i < framer->head_flags; i++)
        hldc_framer_add_byte_unstuffed(framer, out_bits_buf, HLDC_FLAG);

    // Frame data
    for (int i = 0; i < frame_buf->size; i++)
        hldc_framer_add_byte_stuffed(framer, out_bits_buf, frame_buf->data[i]);

    // FCS
    crc_ccitt_t crc_inst;
    crc_ccitt_init(&crc_inst);
    crc_ccitt_update_buffer(&crc_inst, frame_buf->data, frame_buf->size);
    uint16_t crc = crc_ccitt_get(&crc_inst);
    hldc_framer_add_byte_stuffed(framer, out_bits_buf, crc & 0xFF);
    hldc_framer_add_byte_stuffed(framer, out_bits_buf, crc >> 8);

    // Tail flags
    for (int i = 0; i < framer->tail_flags; i++)
        hldc_framer_add_byte_unstuffed(framer, out_bits_buf, HLDC_FLAG);

    LOGV("framed packet: %d bits", out_bits_buf->size);

    if (out_crc != NULL)
        *out_crc = crc;
        
    return HLDC_SUCCESS;
}

static void hldc_deframer_reset(hldc_deframer_t *deframer)
{
    deframer->bytes_len = 0;
    deframer->raw_bits = 0;
    deframer->unstuffed_bits = 0;
    deframer->unstuffed_bit_count = 0;
    deframer->ones_count = 0;
}

static hldc_error_e hldc_deframer_process_flag(hldc_deframer_t *deframer, buffer_t *out_frame_buf, uint16_t *out_crc)
{
    hldc_error_e ret = -HLDC_OTHER;

    int frame_len = deframer->bytes_len - 2;

    if (frame_len <= 0 || out_frame_buf == NULL)
    {
        // Consecutive flags or no output provided, nothing to do, not an error
        ret = 0;
    }
    else if (frame_len < deframer->min_frame_size)
    {
        LOGD("frame too small - %d < %d", frame_len, deframer->min_frame_size);
        ret = -HLDC_FRAME_TOO_SMALL;
    }
    else if (frame_len <= out_frame_buf->capacity)
    {
        // Extract received FCS
        uint16_t received_fcs = deframer->bytes[frame_len + 1];
        received_fcs <<= 8;
        received_fcs |= deframer->bytes[frame_len];

        // Compute actual FCS
        crc_ccitt_t crc_inst;
        crc_ccitt_init(&crc_inst);
        crc_ccitt_update_buffer(&crc_inst, deframer->bytes, frame_len);
        uint16_t computed_fcs = crc_ccitt_get(&crc_inst);

        if (computed_fcs == received_fcs)
        {
            // Copy the frame data to the buffer
            memcpy(out_frame_buf->data, deframer->bytes, frame_len);
            out_frame_buf->size = frame_len;
            if (out_crc)
                *out_crc = received_fcs;
            ret = HLDC_SUCCESS;
            LOGV("deframed packet: %d bytes", frame_len);
        }
        else
        {
            ret = -HLDC_INVALID_FCS;
            LOGD("invalid FCS - calc 0x%04X, recv 0x%04X", computed_fcs, received_fcs);
        }
    }
    else
    {
        ret = -HLDC_BUF_TOO_SMALL;
        LOGV("frame of size %d is too small for output buf of capacity %d", deframer->bytes_len - 2, out_frame_buf->capacity);
    }

    hldc_deframer_reset(deframer);
    return ret;
}

static hldc_error_e hldc_deframer_process_bit(hldc_deframer_t *deframer, int bit)
{
    // Unstuffing
    if (bit) // (bit == 1)
    {
        deframer->unstuffed_bits = (deframer->unstuffed_bits >> 1) | (bit << 7);
        deframer->ones_count++;
        deframer->unstuffed_bit_count++;
    }
    else // (bit == 0)
    {
        if (deframer->ones_count < 5)
        {
            deframer->unstuffed_bits = (deframer->unstuffed_bits >> 1) | (bit << 7);
            deframer->unstuffed_bit_count++;
        }
        deframer->ones_count = 0; // Reset the ones count
    }

    // If we have a full byte, store it in the buffer
    if (deframer->unstuffed_bit_count == 8)
    {
        if (deframer->bytes_len >= sizeof(deframer->bytes))
        {
            LOGD("buffer overflow, resetting");
            hldc_deframer_reset(deframer); // Buffer would overflow, start writing from beginning
        }

        deframer->bytes[deframer->bytes_len++] = deframer->unstuffed_bits;
        deframer->unstuffed_bit_count = 0;
    }

    return 0;
}

void hldc_deframer_init(hldc_deframer_t *deframer)
{
    nonnull(deframer, "deframer");

    hldc_deframer_reset(deframer);
    deframer->min_frame_size = 18; // For AX.25 = 18, for raw HLDC = 3
}

hldc_error_e hldc_deframer_process(hldc_deframer_t *deframer, int bit, buffer_t *out_frame_buf, uint16_t *out_crc)
{
    nonnull(deframer, "deframer");
    assert_buffer_valid(out_frame_buf);

    // Reverse NRZI linecode
    bit = nrzi_decode(bit, &deframer->last_bit);

    // Shift in the new bit
    deframer->raw_bits <<= 1;
    deframer->raw_bits |= bit;

    if (deframer->raw_bits == HLDC_FLAG)
        return hldc_deframer_process_flag(deframer, out_frame_buf, out_crc);

    return hldc_deframer_process_bit(deframer, bit);
}
