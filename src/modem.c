#include "modem.h"
#include "common.h"
#include "buffer.h"
#include <string.h>
#include <math.h>

const float mark_freq = 1200.0f;
const float space_freq = 2200.0f;
const float baud_rate = 1200.0f;

void md_multi_rx_init(struct md_multi_rx *mrx, float sample_rate, demod_type_t types)
{
    nonnull(mrx, "mrx");
    nonzero(sample_rate, "sample_rate");
    nonzero(types, "types");

    int mask = 1;
    mrx->count = 0;
    while (mrx->count <= MD_RX_MAX && mask <= 0x01000000)
    {
        if ((types & mask) > 0)
            md_rx_init(&mrx->rxs[mrx->count++], sample_rate, mask);
        mask <<= 1;
    }

    mrx->last_modem = -1;
    mrx->last_crc = 0;
    mrx->last_time = 0L;
}

int md_multi_rx_process(struct md_multi_rx *mrx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf)
{
    return md_multi_rx_process_at(mrx, sample_buf, out_frame_buf, time(NULL));
}

int md_multi_rx_process_at(struct md_multi_rx *mrx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf, time_t time)
{
    nonnull(mrx, "mrx");
    assert_buffer_valid(sample_buf);
    assert_buffer_valid(out_frame_buf);

    int ret = 0;
    uint16_t crc = 0;
    int modem = -2;

    for (int i = 0; i < mrx->count; i++)
    {
        struct md_rx *rx = &mrx->rxs[i];
        if (ret == 0)
        {
            ret = md_rx_process(rx, sample_buf, out_frame_buf, &crc);
            if (ret > 0)
                modem = i;
        }
        else
            (void)md_rx_process(rx, sample_buf, NULL, NULL);
    }

    if (ret > 0)
    {
        // If just (within 1s) seen identical frame on another modem
        if (crc == mrx->last_crc && modem != mrx->last_modem && time - mrx->last_time <= 1)
            ret = 0; // Pretend there was no frame received

        mrx->last_modem = modem;
        mrx->last_crc = crc;
        mrx->last_time = time;
    }

    return ret;
}

void md_multi_rx_free(struct md_multi_rx *mrx)
{
    nonnull(mrx, "mrx");

    for (int i = 0; i < mrx->count; i++)
        md_rx_free(&mrx->rxs[i]);
    mrx->count = 0;
}

void md_rx_init(struct md_rx *rx, float sample_rate, demod_type_t type)
{
    nonnull(rx, "rx");
    nonzero(type, "type");

    demod_init(&rx->demod, type, &(demod_params_t){.mark_freq = mark_freq, .space_freq = space_freq, .baud_rate = baud_rate, .sample_rate = sample_rate});

    hldc_deframer_init(&rx->deframer);
    bitclk_init(&rx->bit_detector, sample_rate, baud_rate);
}

int md_rx_process(struct md_rx *rx, const float_buffer_t *sample_buf, buffer_t *out_frame_buf, uint16_t *out_crc)
{
    nonnull(rx, "rx");
    assert_buffer_valid(sample_buf);
    // assert_buffer_valid(out_frame_buf); // Can be NULL

    int ret = 0;
    uint16_t ret_crc = 0;

    for (int i = 0; i < sample_buf->size; i++)
    {
        float sample = sample_buf->data[i];
        float symbol = demod_process(&rx->demod, sample);

        int bit = bitclk_detect(&rx->bit_detector, symbol);

        if (out_frame_buf == NULL)
            continue;

        if (bit != BITCLK_NONE)
        {
            hldc_error_e result = hldc_deframer_process(&rx->deframer, bit, out_frame_buf, &ret_crc);
            if (result < 0)
                LOGV("error %d while processing sample", result);

            if (out_frame_buf->size > 0)
                ret = out_frame_buf->size;
        }
    }

    if (ret > 0 && out_crc != NULL)
        *out_crc = ret_crc;

    return ret;
}

void md_rx_free(struct md_rx *rx)
{
    nonnull(rx, "rx");

    demod_free(&rx->demod);
}

void md_tx_init(struct md_tx *tx, float sample_rate, float tx_delay, float tx_tail)
{
    nonnull(tx, "tx");
    nonzero(sample_rate, "sample_rate");
    nonzero(tx_delay, "tx_delay");
    nonzero(tx_tail, "tx_tail");

    mod_init(&tx->fsk_mod, mark_freq, space_freq, baud_rate, sample_rate);

    int head_flags = (int)ceilf(0.001f * tx_delay * baud_rate / 8.0f);
    int tail_flags = (int)ceilf(0.001f * tx_tail * baud_rate / 8.0f);
    LOGD("head flags = %d", head_flags);
    LOGD("tail flags = %d", tail_flags);
    hldc_framer_init(&tx->framer, head_flags, tail_flags);
}

int md_tx_process(struct md_tx *tx, const buffer_t *frame_buf, float_buffer_t *out_sample_buf, uint16_t *out_crc)
{
    nonnull(tx, "tx");
    assert_buffer_valid(frame_buf);
    assert_buffer_valid(out_sample_buf);

    uint8_t bits_data[8192];
    buffer_t bits_buf = {
        .data = bits_data,
        .capacity = sizeof(bits_data),
        .size = 0}; // TODO consider specialized bit array type
    if (hldc_framer_process(&tx->framer, frame_buf, &bits_buf, out_crc))
        return -1;

    out_sample_buf->size = 0;
    for (int i = 0; i < bits_buf.size; i++)
    {
        int bit = bits_buf.data[i];

        float bit_sample_data[512];
        float_buffer_t bit_sample_buf = {
            .data = bit_sample_data,
            .capacity = sizeof(bit_sample_data) / sizeof(float),
            .size = 0};
        int sc = mod_process(&tx->fsk_mod, bit, &bit_sample_buf);

        if (out_sample_buf->size + sc > out_sample_buf->capacity)
            return -1;
        memcpy(out_sample_buf->data + out_sample_buf->size, bit_sample_buf.data, sc * sizeof(float));
        out_sample_buf->size += sc;
    }

    return out_sample_buf->size;
}

void md_tx_free(struct md_tx *tx)
{
    nonnull(tx, "tx");
}

void modem_init(modem_t *modem, modem_params_t *params)
{
    nonnull(params, "params");
    nonzero(params->sample_rate, "params.sample_rate");
    nonzero(params->types, "params.types");

    md_multi_rx_init(&modem->mrx, params->sample_rate, params->types);
    md_tx_init(&modem->tx, params->sample_rate, params->tx_delay, params->tx_tail);
}

int modem_demodulate(modem_t *modem, const float_buffer_t *sample_buf, buffer_t *out_frame_buf)
{
    nonnull(modem, "modem");
    assert_buffer_valid(sample_buf);
    assert_buffer_valid(out_frame_buf);

    int ret = md_multi_rx_process(&modem->mrx, sample_buf, out_frame_buf);
    if (ret > 0)
        LOGV("demodulated frame: %d bytes", ret);
    return ret;
}

int modem_modulate(modem_t *modem, const buffer_t *frame_buf, float_buffer_t *out_sample_buf)
{
    nonnull(modem, "modem");
    assert_buffer_valid(frame_buf);
    assert_buffer_valid(out_sample_buf);

    uint16_t frame_crc;
    int ret = md_tx_process(&modem->tx, frame_buf, out_sample_buf, &frame_crc);
    if (ret > 0)
        LOGV("modulated frame: %d samples", ret);

    // Treat tx crc as already demodulated to filter out self-demodulations
    modem->mrx.last_crc = frame_crc;
    modem->mrx.last_time = time(NULL);

    return ret;
}

void modem_free(modem_t *modem)
{
    nonnull(modem, "modem");

    md_multi_rx_free(&modem->mrx);
    md_tx_free(&modem->tx);
}
