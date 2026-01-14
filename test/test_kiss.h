#ifndef TEST_KISS_H
#define TEST_KISS_H

#include "test.h"
#include "kiss.h"
#include <string.h>

// Helper function for more concise test bodies
static int feed_bytes(kiss_decoder_t *decoder, const uint8_t *data, size_t length, kiss_message_t *message);

void test_kiss_decoder_init(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    assert_equal_int(decoder.in_frame, 0, "decoder init in_frame");
    assert_equal_int(decoder.escaped, 0, "decoder init escaped");
    assert_equal_int(decoder.buffer_pos, 0, "decoder init buffer_pos");
}

void test_kiss_decoder_empty_frames(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Empty frame: FEND [port_cmd] FEND, port=0, cmd=0
    uint8_t frame1[] = {KISS_FEND, 0x00, KISS_FEND};
    int result = feed_bytes(&decoder, frame1, sizeof(frame1), &message);
    assert_equal_int(result, 1, "empty frame port 0 processed");
    assert_equal_int(message.port, 0, "empty frame port 0");
    assert_equal_int(message.command, 0, "empty frame command 0");
    assert_equal_int(message.data_length, 0, "empty frame data_length");

    // Empty frame: FEND [port_cmd] FEND, but port=1, cmd=0
    uint8_t frame2[] = {KISS_FEND, 0x10, KISS_FEND};
    result = feed_bytes(&decoder, frame2, sizeof(frame2), &message);
    assert_equal_int(result, 1, "empty frame port 1 processed");
    assert_equal_int(message.port, 1, "empty frame port 1");
    assert_equal_int(message.command, 0, "empty frame command 1");

    // Empty frame: FEND [port_cmd] FEND, port=3, cmd=4
    uint8_t frame3[] = {KISS_FEND, 0x34, KISS_FEND};
    result = feed_bytes(&decoder, frame3, sizeof(frame3), &message);
    assert_equal_int(result, 1, "empty frame port 3 cmd 4 processed");
    assert_equal_int(message.port, 3, "empty frame port 3");
    assert_equal_int(message.command, 4, "empty frame command 4");
}

void test_kiss_decoder_data_frame(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Frame with data: FEND [port_cmd] [data...] FEND
    uint8_t frame1[] = {KISS_FEND, 0x20, 'A', 'B', 'C', KISS_FEND}; // port=2, cmd=0, data="ABC"
    int result = feed_bytes(&decoder, frame1, sizeof(frame1), &message);
    assert_equal_int(result, 1, "data frame processed");
    assert_equal_int(message.port, 2, "data frame port");
    assert_equal_int(message.command, 0, "data frame command");
    assert_equal_int(message.data_length, 3, "data frame data_length");
    assert_memory(message.data, (uint8_t *)"ABC", 3, "data frame data");

    // Valid frame: FEND [port_cmd] data FEND, port=0, cmd=0, data="Hello"
    uint8_t frame2[] = {KISS_FEND, 0x00, 'H', 'e', 'l', 'l', 'o', KISS_FEND};
    result = feed_bytes(&decoder, frame2, sizeof(frame2), &message);
    assert_equal_int(result, 1, "valid frame processed");
    assert_equal_int(message.port, 0, "valid frame port");
    assert_equal_int(message.command, 0, "valid frame command");
    assert_equal_int(message.data_length, 5, "valid frame data_length");
    assert_memory(message.data, (uint8_t *)"Hello", 5, "valid frame data");
}

void test_kiss_encode_basic(void)
{
    kiss_message_t message;
    message.port = 0;
    message.command = 0;
    message.data_length = 5;
    memcpy(message.data, "Hello", 5);

    uint8_t buffer[128];
    int len = kiss_encode(&message, buffer, sizeof(buffer));

    assert_equal_int(len, 8, "encoded length"); // FEND, port_cmd, H,e,l,l,o, FEND
    assert_equal_int(buffer[0], KISS_FEND, "frame start");
    assert_equal_int(buffer[1], 0x00, "port_cmd byte"); // port=0, cmd=0
    assert_equal_int(buffer[2], 'H', "data byte 0");
    assert_equal_int(buffer[3], 'e', "data byte 1");
    assert_equal_int(buffer[4], 'l', "data byte 2");
    assert_equal_int(buffer[5], 'l', "data byte 3");
    assert_equal_int(buffer[6], 'o', "data byte 4");
    assert_equal_int(buffer[7], KISS_FEND, "frame end");
}

void test_kiss_encode_with_escaping(void)
{
    kiss_message_t message;
    message.port = 1;
    message.command = 0;
    message.data_length = 6;
    memcpy(message.data, (uint8_t[]){'A', KISS_FEND, 'B', KISS_FESC, 'C', 'D'}, 6);

    uint8_t buffer[128];
    int len = kiss_encode(&message, buffer, sizeof(buffer));

    assert_equal_int(len, 11, "encoded length with escapes"); // FEND, port_cmd, A, FESC, TFEND, B, FESC, TFESC, C, D, FEND
    assert_equal_int(buffer[0], KISS_FEND, "frame start");
    assert_equal_int(buffer[1], 0x10, "port_cmd byte"); // port=1, cmd=0
    assert_equal_int(buffer[2], 'A', "data byte A");
    assert_equal_int(buffer[3], KISS_FESC, "escape FEND start");
    assert_equal_int(buffer[4], KISS_TFEND, "escaped FEND");
    assert_equal_int(buffer[5], 'B', "data byte B");
    assert_equal_int(buffer[6], KISS_FESC, "escape FESC start");
    assert_equal_int(buffer[7], KISS_TFESC, "escaped FESC");
    assert_equal_int(buffer[8], 'C', "data byte C");
    assert_equal_int(buffer[9], 'D', "data byte D");
    assert_equal_int(buffer[10], KISS_FEND, "frame end");
}

void test_kiss_read_frame_escaped_characters(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Frame with escaped characters: FEND [port_cmd] FESC TFEND 'C' 'D' FESC TFESC 'E' 'F' FEND
    // Should decode to: FEND, 'C','D', FESC, 'E','F'
    uint8_t frame[] = {KISS_FEND, 0x00,
                       KISS_FESC, KISS_TFEND, 'C', 'D',
                       KISS_FESC, KISS_TFESC, 'E', 'F',
                       KISS_FEND};
    uint8_t expected_data[] = {KISS_FEND, 'C', 'D', KISS_FESC, 'E', 'F'};

    int result = feed_bytes(&decoder, frame, sizeof(frame), &message);
    assert_equal_int(result, 1, "escaped frame processed");
    assert_equal_int(message.port, 0, "escaped frame port");
    assert_equal_int(message.command, 0, "escaped frame command");
    assert_equal_int(message.data_length, 6, "escaped frame data_length");
    assert_memory(message.data, expected_data, 6, "escaped frame data");
}

void test_kiss_read_invalid_escape_sequence(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Invalid escape: FESC followed by invalid byte, then valid frame
    uint8_t invalid_frame[] = {KISS_FEND, 0x00, KISS_FESC, 0x99};
    uint8_t valid_frame[] = {KISS_FEND, 0x00, 'V', 'a', 'l', 'i', 'd', KISS_FEND};

    // Process invalid frame - should reset
    int result = feed_bytes(&decoder, invalid_frame, sizeof(invalid_frame), &message);
    assert_equal_int(result, 0, "invalid frame not processed");

    // Process valid frame after reset
    result = feed_bytes(&decoder, valid_frame, sizeof(valid_frame), &message);
    assert_equal_int(result, 1, "valid frame processed after reset");
    assert_memory(message.data, (uint8_t *)"Valid", 5, "valid frame data");
}

void test_kiss_read_incomplete_frame(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Incomplete frame: FEND [port_cmd] data (no ending FEND)
    uint8_t frame[] = {KISS_FEND, 0x00, 'U', 'n', 'c', 'o', 'm'};

    int result = feed_bytes(&decoder, frame, sizeof(frame), &message);
    assert_equal_int(result, 0, "incomplete frame not processed");
}

void test_kiss_read_consecutive_empty_frames(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Three frames: two empty, then data frame
    uint8_t empty1[] = {KISS_FEND, 0x00, KISS_FEND};
    uint8_t empty2[] = {KISS_FEND, 0x10, KISS_FEND};
    uint8_t data_frame[] = {KISS_FEND, 0x00, 'D', 'a', 't', 'a', KISS_FEND};

    // Process first empty frame
    int result = feed_bytes(&decoder, empty1, sizeof(empty1), &message);
    assert_equal_int(result, 1, "first empty frame processed");
    assert_equal_int(message.data_length, 0, "first empty frame data_length");

    // Process second empty frame
    result = feed_bytes(&decoder, empty2, sizeof(empty2), &message);
    assert_equal_int(result, 1, "second empty frame processed");
    assert_equal_int(message.data_length, 0, "second empty frame data_length");

    // Process data frame
    result = feed_bytes(&decoder, data_frame, sizeof(data_frame), &message);
    assert_equal_int(result, 1, "data frame processed after empty frames");
    assert_equal_int(message.data_length, 4, "data frame data_length");
    assert_memory(message.data, (uint8_t *)"Data", 4, "data after empty frames");
}

void test_kiss_read_multiple_consecutive_escape(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Frame with FESC followed by FESC (malformed)
    uint8_t frame[] = {KISS_FEND, 0x00, KISS_FESC, KISS_FESC, 'X', KISS_FEND};

    int result = feed_bytes(&decoder, frame, sizeof(frame), &message);
    assert_equal_int(result, 0, "malformed escape frame not processed");
}

void test_kiss_read_back_to_back_frames(void)
{
    kiss_decoder_t decoder;
    kiss_decoder_init(&decoder);
    kiss_message_t message;

    // Three frames back-to-back: "One", "Two", "Three"
    uint8_t frames[] = {KISS_FEND, 0x00, 'O', 'n', 'e', KISS_FEND,
                        KISS_FEND, 0x00, 'T', 'w', 'o', KISS_FEND,
                        KISS_FEND, 0x00, 'T', 'h', 'r', 'e', 'e', KISS_FEND};

    // First frame
    int result = 0;
    size_t i = 0;
    for (i = 0; i < 6 && result == 0; i++)
        result = kiss_decoder_process(&decoder, frames[i], &message);

        assert_equal_int(result, 1, "first back-to-back frame");
    assert_memory(message.data, (uint8_t *)"One", 3, "first frame data");

    // Second frame
    result = 0;
    for (size_t j = 0; j < 6 && result == 0; j++, i++)
        result = kiss_decoder_process(&decoder, frames[i], &message);

        assert_equal_int(result, 1, "second back-to-back frame");
    assert_memory(message.data, (uint8_t *)"Two", 3, "second frame data");

    // Third frame
    result = 0;
    for (size_t j = 0; j < 8 && result == 0; j++, i++)
        if (i < sizeof(frames))
            result = kiss_decoder_process(&decoder, frames[i], &message);

    assert_equal_int(result, 1, "third back-to-back frame");
    assert_memory(message.data, (uint8_t *)"Three", 5, "third frame data");
}

static int feed_bytes(kiss_decoder_t *decoder, const uint8_t *data, size_t length, kiss_message_t *message)
{
    for (size_t i = 0; i < length; i++)
    {
        int result = kiss_decoder_process(decoder, data[i], message);
        if (result != 0)
            return result;
    }
    return 0; // No frame processed
}

#endif
