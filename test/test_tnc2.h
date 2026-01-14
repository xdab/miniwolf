#ifndef TEST_TNC2_H
#define TEST_TNC2_H

#include "test.h"
#include <string.h>
#include "tnc2.h"
#include "ax25.h"
#include "buffer.h"

void test_tnc2_string_to_packet_with_repeated()
{
    ax25_packet_t packet;
    const char *str = "N0CALL>APN001,RPTD*:test!abcdefghijkl";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, 0, "repeated packet parse");
    assert_equal_int(packet.path_len, 1, "repeated packet path len");
    assert_equal_int(packet.path[0].repeated, 1, "repeated flag set");
    assert_equal_int(packet.info_len, 17, "repeated packet info len");
}

void test_tnc2_roundtrip_simple()
{
    ax25_packet_t packet1, packet2;
    unsigned char buf_data[256];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};

    ax25_packet_init(&packet1);
    ax25_addr_init_with(&packet1.source, "SRC", 0, 0);
    ax25_addr_init_with(&packet1.destination, "DST", 0, 0);
    memcpy(packet1.info, "test", 4);
    packet1.info_len = 4;

    int len = tnc2_packet_to_string(&packet1, &buf);
    assert_true(len > 0, "roundtrip encode");

    int ret = tnc2_string_to_packet(&packet2, &buf);
    assert_equal_int(ret, 0, "roundtrip decode");
    assert_equal_int(packet2.info_len, 4, "roundtrip info len");
    assert_equal_int(packet2.path_len, 0, "roundtrip no path");
}

void test_tnc2_roundtrip_complex()
{
    ax25_packet_t packet1, packet2;
    unsigned char buf_data[256];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};

    ax25_packet_init(&packet1);
    ax25_addr_init_with(&packet1.source, "SRC", 1, 0);
    ax25_addr_init_with(&packet1.destination, "DST", 2, 0);
    packet1.path_len = 2;
    ax25_addr_init_with(&packet1.path[0], "DIG1", 3, 0);
    ax25_addr_init_with(&packet1.path[1], "DIG2", 4, 1);
    memcpy(packet1.info, "complex", 7);
    packet1.info_len = 7;

    int len = tnc2_packet_to_string(&packet1, &buf);
    assert_true(len > 0, "complex roundtrip encode");

    int ret = tnc2_string_to_packet(&packet2, &buf);
    assert_equal_int(ret, 0, "complex roundtrip decode");
    assert_equal_int(packet2.source.ssid, 1, "complex source ssid");
    assert_equal_int(packet2.destination.ssid, 2, "complex dest ssid");
    assert_equal_int(packet2.path_len, 2, "complex path count");
    assert_equal_int(packet2.path[1].repeated, 1, "complex repeated flag");
}

void test_tnc2_invalid_chars_in_callsign()
{
    ax25_packet_t packet;
    const char *str = "SRC@>DEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject special char in callsign");
}

void test_tnc2_control_char()
{
    ax25_packet_t packet;
    const char *str = "SRC\x00>DEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = 14, .size = 14};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject null byte in callsign");
}

void test_tnc2_ssid_overflow()
{
    ax25_packet_t packet;
    const char *str = "SRC-999>DEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject SSID overflow");
}

void test_tnc2_missing_greater_than()
{
    ax25_packet_t packet;
    const char *str = "SRCDEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject missing >");
}

void test_tnc2_missing_colon()
{
    ax25_packet_t packet;
    const char *str = "SRC>DESTinfo";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject missing :");
}

void test_tnc2_empty_callsign()
{
    ax25_packet_t packet;
    const char *str = "->DEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject empty callsign");
}

void test_tnc2_too_many_digis()
{
    ax25_packet_t packet;
    const char *str = "SRC>DEST,D1,D2,D3,D4,D5,D6,D7,D8,D9:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject excess digipeaters");
}

void test_tnc2_callsign_too_long()
{
    ax25_packet_t packet;
    const char *str = "ABCDEFGHIJ>DEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject callsign > 6 chars");
}

void test_tnc2_space_in_callsign()
{
    ax25_packet_t packet;
    const char *str = "SR C>DEST:info";
    buffer_t buf = {.data = (unsigned char *)str, .capacity = strlen(str), .size = strlen(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject space in callsign");
}

void test_tnc2_info_too_large()
{
    ax25_packet_t packet;
    char str[600];
    memcpy(str, "SRC>DEST:", 9);
    memset(str + 9, 'X', 550);
    buffer_t buf = {.data = (unsigned char *)str, .capacity = sizeof(str), .size = sizeof(str)};
    int ret = tnc2_string_to_packet(&packet, &buf);
    assert_equal_int(ret, -1, "reject oversized info");
}

void test_tnc2_null_termination()
{
    ax25_addr_t addr;
    ax25_addr_init_with(&addr, "CALL", 5, 0);

    // Test that output is null-terminated when there's space
    unsigned char buf_data[20];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};
    int n = tnc2_addr_to_string(&addr, &buf);
    assert_equal_int(n, 6, "null term addr length");
    assert_equal_int(buf.size, 6, "null term buffer size");
    assert_string((char *)buf.data, "CALL-5", "null term addr content");
    // Check that null terminator is present
    assert_equal_int(buf.data[6], '\0', "null terminator present");

    // Test with buffer exactly sized
    unsigned char small_buf[7]; // 6 chars + 1 null = 7
    buffer_t small = {.data = small_buf, .capacity = sizeof(small_buf), .size = 0};
    n = tnc2_addr_to_string(&addr, &small);
    assert_equal_int(n, 6, "small buf addr length");
    assert_equal_int(small.size, 6, "small buf buffer size");
    assert_string((char *)small.data, "CALL-5", "small buf addr content");
    assert_equal_int(small.data[6], '\0', "small buf null terminator");

    // Test with buffer too small for null terminator - should still work but no null term
    unsigned char tiny_buf[6]; // Exactly 6 chars, no room for null
    buffer_t tiny = {.data = tiny_buf, .capacity = sizeof(tiny_buf), .size = 0};
    n = tnc2_addr_to_string(&addr, &tiny);
    assert_equal_int(n, -1, "tiny buf addr fails due to no null term space");
    assert_equal_int(tiny.size, 0, "tiny buf buffer size unchanged on failure");
}

void test_tnc2_packet_roundtrip_with_null_term()
{
    ax25_packet_t packet1, packet2;
    ax25_packet_init(&packet1);
    ax25_addr_init_with(&packet1.source, "SRC", 0, 0);
    ax25_addr_init_with(&packet1.destination, "DST", 0, 0);
    memcpy(packet1.info, "HELLO APRS", 10);
    packet1.info_len = 10;

    // Convert to string
    unsigned char str_buf[256];
    buffer_t str = {.data = str_buf, .capacity = sizeof(str_buf), .size = 0};
    int encode_len = tnc2_packet_to_string(&packet1, &str);
    assert_true(encode_len > 0, "roundtrip encode success");

    // Check that result is null-terminated if possible
    if (encode_len < str.capacity)
    {
        assert_equal_int(str.data[encode_len], '\0', "packet string null terminated");
    }

    // Convert back to packet
    int decode_ret = tnc2_string_to_packet(&packet2, &str);
    assert_equal_int(decode_ret, 0, "roundtrip decode success");
    assert_equal_int(packet2.info_len, 10, "roundtrip info len");
    assert_equal_int(memcmp(packet2.info, "HELLO APRS", 10), 0, "roundtrip info content");
}

void test_tnc2_edge_case_mixed_valid_invalid_chars()
{
    ax25_packet_t packet;
    // Test callsign with valid chars followed by invalid
    const char *str1 = "ABC123@>DEST:info";
    buffer_t buf1 = {.data = (unsigned char *)str1, .capacity = strlen(str1), .size = strlen(str1)};
    int ret1 = tnc2_string_to_packet(&packet, &buf1);
    assert_equal_int(ret1, -1, "reject valid chars followed by invalid");

    // Test callsign with invalid chars in middle
    const char *str2 = "AB@C>DEST:info";
    buffer_t buf2 = {.data = (unsigned char *)str2, .capacity = strlen(str2), .size = strlen(str2)};
    int ret2 = tnc2_string_to_packet(&packet, &buf2);
    assert_equal_int(ret2, -1, "reject invalid char in middle of callsign");

    // Test callsign ending with valid chars
    const char *str3 = "ABC>DEST:info";
    buffer_t buf3 = {.data = (unsigned char *)str3, .capacity = strlen(str3), .size = strlen(str3)};
    int ret3 = tnc2_string_to_packet(&packet, &buf3);
    assert_equal_int(ret3, 0, "accept callsign with only valid chars");
}

void test_tnc2_edge_case_boundary_digits()
{
    ax25_packet_t packet;
    // Test SSID boundary values
    const char *str1 = "CALL-15>DEST:info"; // Max valid SSID
    buffer_t buf1 = {.data = (unsigned char *)str1, .capacity = strlen(str1), .size = strlen(str1)};
    int ret1 = tnc2_string_to_packet(&packet, &buf1);
    assert_equal_int(ret1, 0, "accept max SSID 15");
    assert_equal_int(packet.source.ssid, 15, "max SSID value");

    // Test SSID just over boundary
    const char *str2 = "CALL-16>DEST:info"; // Invalid SSID
    buffer_t buf2 = {.data = (unsigned char *)str2, .capacity = strlen(str2), .size = strlen(str2)};
    int ret2 = tnc2_string_to_packet(&packet, &buf2);
    assert_equal_int(ret2, -1, "reject SSID 16");

    // Test zero-padded SSID
    const char *str3 = "CALL-0>DEST:info"; // Valid zero SSID
    buffer_t buf3 = {.data = (unsigned char *)str3, .capacity = strlen(str3), .size = strlen(str3)};
    int ret3 = tnc2_string_to_packet(&packet, &buf3);
    assert_equal_int(ret3, 0, "accept zero SSID");
    assert_equal_int(packet.source.ssid, 0, "zero SSID value");
}

void test_tnc2_edge_case_callsign_padding()
{
    ax25_addr_t addr;
    ax25_addr_init_with(&addr, "AB", 0, 0); // Short callsign

    unsigned char buf_data[20];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};
    int n = tnc2_addr_to_string(&addr, &buf);
    assert_equal_int(n, 2, "short callsign length");
    assert_equal_int(buf.size, 2, "short callsign buffer size");
    assert_string((char *)buf.data, "AB", "short callsign content");

    // Test that padding spaces are not included in output
    ax25_addr_init_with(&addr, "A", 0, 0); // Single char callsign
    buf.size = 0;
    n = tnc2_addr_to_string(&addr, &buf);
    assert_equal_int(n, 1, "single char callsign length");
    assert_string((char *)buf.data, "A", "single char callsign content");
}
#endif
