#ifndef TEST_HLDC_H
#define TEST_HLDC_H

#include "test.h"
#include "hldc.h"
#include "crc.h"
#include <string.h>

void test_hldc_framer_init(void)
{
    hldc_framer_t framer;
    hldc_framer_init(&framer, 2, 3);
    assert_equal_int(framer.head_flags, 2, "head flags initialized");
    assert_equal_int(framer.tail_flags, 3, "tail flags initialized");
    assert_equal_int(framer.nrzi_bit, 0, "nrzi bit initialized");
    assert_equal_int(framer.ones_count, 0, "ones count initialized");
}

void test_hldc_framer_flag_scaling(void)
{
    hldc_framer_t framer_no_flags, framer_with_flags;
    hldc_framer_init(&framer_no_flags, 0, 0);
    hldc_framer_init(&framer_with_flags, 1, 1); // 1 head + 1 tail flag

    uint8_t bits_no_flags[256], bits_with_flags[256];
    buffer_t bits_buf_no_flags = {.data = bits_no_flags, .capacity = sizeof(bits_no_flags), .size = 0};
    buffer_t bits_buf_with_flags = {.data = bits_with_flags, .capacity = sizeof(bits_with_flags), .size = 0};

    uint8_t data[] = {0x00}; // Use 0x00 to avoid any bit stuffing complications
    buffer_t data_buf = {.data = data, .capacity = sizeof(data), .size = sizeof(data)};

    hldc_error_e err_no_flags = hldc_framer_process(&framer_no_flags, &data_buf, &bits_buf_no_flags, NULL);
    hldc_error_e err_with_flags = hldc_framer_process(&framer_with_flags, &data_buf, &bits_buf_with_flags, NULL);

    assert_equal_int(err_no_flags, HLDC_SUCCESS, "no flags framing succeeds");
    assert_equal_int(err_with_flags, HLDC_SUCCESS, "with flags framing succeeds");
    assert_true((bits_buf_with_flags.size - bits_buf_no_flags.size) == 16, "flags add 8 bits per flag byte");
}

void test_hldc_framer_bit_stuffing(void)
{
    hldc_framer_t framer;
    hldc_framer_init(&framer, 0, 0);

    uint8_t bits[256];
    buffer_t bits_buf = {.data = bits, .capacity = sizeof(bits), .size = 0};

    uint8_t data_stuffing[] = {0xff}; // 8 consecutive ones
    buffer_t data_buf = {.data = data_stuffing, .capacity = sizeof(data_stuffing), .size = sizeof(data_stuffing)};

    hldc_error_e err = hldc_framer_process(&framer, &data_buf, &bits_buf, NULL);
    assert_equal_int(err, HLDC_SUCCESS, "bit stuffing framing succeeds");

    // 9 (stuffed 0xff) + 16 (CRC) + 1 (CRC stuffing)
    assert_equal_int(bits_buf.size, 9 + 16 + 1, "bit stuffing works");
}

void test_hldc_deframer_init(void)
{
    hldc_deframer_t deframer;
    hldc_deframer_init(&deframer);
    assert_equal_int(deframer.bytes_len, 0, "deframer bytes len initialized");
    assert_equal_int(deframer.unstuffed_bit_count, 0, "unstuffed bit count initialized");
    assert_equal_int(deframer.ones_count, 0, "ones count initialized");
}

#endif
