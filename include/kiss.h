#ifndef KISS_H
#define KISS_H

#define KISS_FEND 0xC0
#define KISS_FESC 0xDB
#define KISS_TFEND 0xDC
#define KISS_TFESC 0xDD

#define KISS_DATA_FRAME 0x00
#define KISS_MIN_FRAME_SIZE 1

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t port;
    uint8_t command;
    uint8_t data_length;
    uint8_t data[256];
} kiss_message_t;

int kiss_encode(const kiss_message_t *message, uint8_t *buffer, int buffer_len);

typedef struct
{
    uint8_t buffer[256];
    size_t buffer_pos;
    int in_frame;
    int escaped;
} kiss_decoder_t;

void kiss_decoder_init(kiss_decoder_t *decoder);

int kiss_decoder_process(kiss_decoder_t *decoder, uint8_t byte, kiss_message_t *output);

#endif
