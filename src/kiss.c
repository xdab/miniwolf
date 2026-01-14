#include "kiss.h"
#include "ax25.h"
#include "common.h"
#include <string.h>

static inline void reset_decoder(kiss_decoder_t *decoder)
{
    decoder->in_frame = 0;
    decoder->escaped = 0;
    decoder->buffer_pos = 0;
}

static inline int buffer_safe_write(kiss_decoder_t *decoder, uint8_t byte)
{
    if (decoder->buffer_pos < sizeof(decoder->buffer))
    {
        decoder->buffer[decoder->buffer_pos++] = byte;
        return 1;
    }
    else
    {
        reset_decoder(decoder);
        return 0;
    }
}

static void kiss_message_init(kiss_message_t *message)
{
    message->port = 0;
    message->command = KISS_DATA_FRAME;
    message->data_length = 0;
}

int kiss_encode(const kiss_message_t *message, uint8_t *buffer, int buffer_len)
{
    nonnull(message, "message");
    nonnull(buffer, "buffer");
    EXITIF(buffer_len < 3, -1, "buffer_len must be greater than 3");

    int pos = 0;

    // Append start of frame
    buffer[pos++] = KISS_FEND;

    // Encode (port, command) byte
    uint8_t port_command = (message->port << 4) | message->command;
    buffer[pos++] = port_command;

    // Copy data with escape sequences
    for (size_t i = 0; i < message->data_length; i++)
    {
        uint8_t byte = message->data[i];
        if (byte == KISS_FEND)
        {
            // Escape FEND
            if (pos + 2 > buffer_len)
                return -1;
            buffer[pos++] = KISS_FESC;
            buffer[pos++] = KISS_TFEND;
        }
        else if (byte == KISS_FESC)
        {
            // Escape FESC
            if (pos + 2 > buffer_len)
                return -1;
            buffer[pos++] = KISS_FESC;
            buffer[pos++] = KISS_TFESC;
        }
        else
        {
            // Copy byte as-is
            if (pos + 1 > buffer_len)
                return -1;
            buffer[pos++] = byte;
        }
    }

    // Append end of frame
    if (pos + 1 > buffer_len)
        return -1;
    buffer[pos++] = KISS_FEND;

    return pos;
}

void kiss_decoder_init(kiss_decoder_t *decoder)
{
    nonnull(decoder, "decoder");

    reset_decoder(decoder);
}

static int kiss_decoder_process_frame(kiss_decoder_t *decoder, kiss_message_t *output)
{
    // Interpret (port, command)
    output->port = (decoder->buffer[0] >> 4) & 0x0f;
    output->command = decoder->buffer[0] & 0x0f;
    output->data_length = decoder->buffer_pos - 1;

    // Copy data
    size_t max_data = sizeof(output->data);
    if (output->data_length > max_data)
        output->data_length = max_data;

    memcpy(output->data, decoder->buffer + 1, output->data_length);

    return 1;
}

int kiss_decoder_process(kiss_decoder_t *decoder, uint8_t byte, kiss_message_t *output)
{
    nonnull(decoder, "decoder");
    nonnull(output, "output");

    if (byte == KISS_FEND)
    {
        if (!decoder->in_frame)
        {
            decoder->in_frame = 1;
            decoder->buffer_pos = 0;
        }
        else
        {
            // Handle apparent end of frame
            if (decoder->buffer_pos > 0)
            {
                int result = kiss_decoder_process_frame(decoder, output);
                if (result == 1)
                    reset_decoder(decoder);
                return result;
            }
            // Empty frame (FEND without port_cmd) - just reset
            reset_decoder(decoder);
        }
    }
    else if (byte == KISS_FESC)
    {
        if (decoder->escaped)
            reset_decoder(decoder); // Double escape, reset frame
        else
            decoder->escaped = 1;
    }
    else if (byte == KISS_TFEND)
    {
        if (decoder->escaped)
        {
            buffer_safe_write(decoder, KISS_FEND);
            decoder->escaped = 0;
        }
        else
            buffer_safe_write(decoder, KISS_TFEND);
    }
    else if (byte == KISS_TFESC)
    {
        if (decoder->escaped)
        {
            buffer_safe_write(decoder, KISS_FESC);
            decoder->escaped = 0;
        }
        else
            buffer_safe_write(decoder, KISS_TFESC);
    }
    else
    {
        if (decoder->escaped)
            reset_decoder(decoder); // Invalid escape, reset frame
        else
            buffer_safe_write(decoder, byte);
    }

    return 0;
}
