#ifndef TEST_AX25_H
#define TEST_AX25_H

#include "test.h"
#include <string.h>
#include "ax25.h"

void test_addr_init()
{
    ax25_addr_t addr;
    ax25_addr_init(&addr);
    assert_memory(addr.callsign, "      ", 6, "addr init");
    assert_equal_int(addr.ssid, 0, "addr init ssid");
    assert_equal_int(addr.repeated, 0, "addr init repeated");
    assert_equal_int(addr.last, 0, "addr init last");
}

void test_addr_init_with()
{
    ax25_addr_t addr;
    ax25_addr_init_with(&addr, "TEST", 5, 1);
    assert_memory(addr.callsign, "TEST  ", 6, "addr init with callsign");
    assert_equal_int(addr.ssid, 5, "addr init with ssid");
    assert_equal_int(addr.repeated, 1, "addr init with repeated");
    assert_equal_int(addr.last, 0, "addr init with last");
}

void test_addr_pack()
{
    ax25_addr_t addr;
    ax25_addr_init_with(&addr, "TES", 1, 1);
    addr.last = 1;
    uint8_t buf_data[7];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};
    ax25_error_e pack_result = ax25_addr_pack(&addr, &buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");
    assert_equal_int(buf.data[0], 'T' << 1, "pack buf[0]");
    assert_equal_int(buf.data[1], 'E' << 1, "pack buf[1]");
    assert_equal_int(buf.data[2], 'S' << 1, "pack buf[2]");
    assert_equal_int(buf.data[3], ' ' << 1, "pack buf[3]");
    assert_equal_int(buf.data[4], ' ' << 1, "pack buf[4]");
    assert_equal_int(buf.data[5], ' ' << 1, "pack buf[5]");
    assert_equal_int(buf.data[6], 0b01100000 | (1 << 7) | (1 << 1) | 1, "pack buf[6]");
}

void test_addr_unpack()
{
    ax25_addr_t addr;
    uint8_t buf_data[7] = {
        ('T' << 1),
        ('E' << 1),
        ('S' << 1),
        (AX25_ADDR_PAD << 1),
        (AX25_ADDR_PAD << 1),
        (AX25_ADDR_PAD << 1),
        0b01100000 | (1 << 7) | (5 << 1)};
    buffer_t buf = {
        .data = buf_data,
        .capacity = sizeof(buf_data),
        .size = sizeof(buf_data)};
    ax25_error_e unpack_result = ax25_addr_unpack(&addr, &buf);
    assert_equal_int(unpack_result, AX25_SUCCESS, "unpack success");
    assert_memory(addr.callsign, "TES   ", 6, "unpack callsign");
    assert_equal_int(addr.ssid, 5, "unpack ssid");
    assert_equal_int(addr.repeated, 1, "unpack repeated");
    assert_equal_int(addr.last, 0, "unpack last");
}

void test_packet_init()
{
    ax25_packet_t packet;
    ax25_packet_init(&packet);
    assert_memory(packet.source.callsign, "      ", 6, "packet init source");
    assert_memory(packet.destination.callsign, "      ", 6, "packet init dest");
    assert_equal_int(packet.path_len, 0, "init path_len");
    assert_equal_int(packet.control, 0x03, "init control");
    assert_equal_int(packet.protocol, 0xF0, "init protocol");
    assert_equal_int(packet.info_len, 0, "init info_len");
}

void test_packet_pack()
{
    ax25_packet_t packet;
    ax25_packet_init(&packet);
    ax25_addr_init_with(&packet.source, "SRC", 1, 0);
    ax25_addr_init_with(&packet.destination, "DST", 2, 0);
    packet.path_len = 1;
    ax25_addr_init_with(&packet.path[0], "DIG", 3, 0);
    uint8_t buf_data[100];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&packet, &buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");
    assert_true(buf.size > 0, "packet size stored in buf.size");
}

void test_packet_pack_unpack()
{
    // Create original packet
    ax25_packet_t original;
    ax25_packet_init(&original);
    ax25_addr_init_with(&original.source, "SOURCE", 1, 0);
    ax25_addr_init_with(&original.destination, "DEST", 2, 0);
    original.path_len = 1;
    ax25_addr_init_with(&original.path[0], "DIGI", 3, 1);
    original.control = 0x10;
    original.protocol = 0xF0;
    const char *info = "Hello World!";
    memcpy(original.info, info, strlen(info));
    original.info_len = strlen(info);

    // Pack
    uint8_t buf_data[256];
    buffer_t buf = {.data = buf_data, .capacity = sizeof(buf_data), .size = 0};
    ax25_error_e pack_result = ax25_packet_pack(&original, &buf);
    assert_equal_int(pack_result, AX25_SUCCESS, "pack success");
    assert_true(buf.size > 0, "packet size stored in buf.size");

    // Unpack
    ax25_packet_t unpacked;
    ax25_error_e unpack_result = ax25_packet_unpack(&unpacked, &buf);
    assert_equal_int(unpack_result, AX25_SUCCESS, "unpack success");

    // Check fields
    assert_memory(unpacked.source.callsign, original.source.callsign, 6, "source callsign");
    assert_equal_int(unpacked.source.ssid, original.source.ssid, "source ssid");
    assert_equal_int(unpacked.source.repeated, original.source.repeated, "source repeated");

    assert_memory(unpacked.destination.callsign, original.destination.callsign, 6, "dest callsign");
    assert_equal_int(unpacked.destination.ssid, original.destination.ssid, "dest ssid");
    assert_equal_int(unpacked.destination.repeated, original.destination.repeated, "dest repeated");

    assert_equal_int(unpacked.path_len, original.path_len, "path_len");
    assert_memory(unpacked.path[0].callsign, original.path[0].callsign, 6, "path callsign");
    assert_equal_int(unpacked.path[0].ssid, original.path[0].ssid, "path ssid");
    assert_equal_int(unpacked.path[0].repeated, original.path[0].repeated, "path repeated");

    assert_equal_int(unpacked.control, original.control, "control");
    assert_equal_int(unpacked.protocol, original.protocol, "protocol");
    assert_equal_int(unpacked.info_len, original.info_len, "info_len");
    assert_memory(unpacked.info, original.info, original.info_len, "info");
}

#endif
