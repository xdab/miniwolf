#include "ax25.h"
#include "common.h"
#include <string.h>

void ax25_addr_init(ax25_addr_t *addr)
{
    nonnull(addr, "addr");

    memset(addr->callsign, AX25_ADDR_PAD, AX25_ADDR_MAX_CALLSIGN_LEN);

    addr->ssid = 0;
    addr->repeated = false;
    addr->last = false;
}

void ax25_addr_init_with(ax25_addr_t *addr, const char *callsign, int ssid, bool repeated)
{
    nonnull(addr, "addr");
    nonnull(callsign, "callsign");

    size_t len = strnlen(callsign, AX25_ADDR_MAX_CALLSIGN_LEN);
    memcpy(addr->callsign, callsign, len);
    memset(addr->callsign + len, AX25_ADDR_PAD, AX25_ADDR_MAX_CALLSIGN_LEN - len);

    addr->ssid = ssid;
    addr->repeated = repeated;
    addr->last = false;
}

ax25_error_e ax25_addr_pack(const ax25_addr_t *addr, buffer_t *out_buf)
{
    nonnull(addr, "addr");
    assert_buffer_valid(out_buf);

    if (!buf_has_capacity_ge(out_buf, AX25_ADDR_LEN))
        return -AX25_ADDR_BUF_TOO_SMALL;

    for (int i = 0; i < AX25_ADDR_MAX_CALLSIGN_LEN; i++)
        out_buf->data[i] = addr->callsign[i] << 1;

    out_buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] = 0b01100000;
    out_buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] |= addr->repeated << 7;
    out_buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] |= addr->ssid << 1;
    out_buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] |= addr->last;

    out_buf->size = AX25_ADDR_LEN;

    return AX25_SUCCESS;
}

ax25_error_e ax25_addr_unpack(ax25_addr_t *addr, const buffer_t *buf)
{
    nonnull(addr, "addr");
    assert_buffer_valid(buf);

    if (!buf_has_size_ge(buf, AX25_ADDR_LEN))
        return -AX25_ADDR_BUF_TOO_SMALL;

    for (int i = 0; i < AX25_ADDR_MAX_CALLSIGN_LEN; i++)
        addr->callsign[i] = (buf->data[i] >> 1) & 0x7f;

    addr->repeated = (buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] >> 7) & 1;
    addr->ssid = (buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] >> 1) & 0x0f;
    addr->last = buf->data[AX25_ADDR_MAX_CALLSIGN_LEN] & 1;

    return AX25_SUCCESS;
}

void ax25_packet_init(ax25_packet_t *packet)
{
    nonnull(packet, "packet");

    ax25_addr_init(&packet->source);
    ax25_addr_init(&packet->destination);
    packet->path_len = 0;
    packet->control = 0x03;
    packet->protocol = 0xF0;
    packet->info_len = 0;
}

int ax25_packet_len(const ax25_packet_t *packet)
{
    return AX25_MIN_PACKET_LEN                // source addr + destination addr + control + protocol)
           + packet->path_len * AX25_ADDR_LEN // path addrs
           + packet->info_len;
}

ax25_error_e ax25_packet_pack(const ax25_packet_t *packet, buffer_t *out_buf)
{
    nonnull(packet, "packet");
    assert_buffer_valid(out_buf);

    if (out_buf->capacity < ax25_packet_len(packet))
        return -AX25_BUF_TOO_SMALL;

    out_buf->size = 0;

    buffer_t addr_buf = {
        .data = &out_buf->data[out_buf->size],
        .capacity = out_buf->capacity - out_buf->size,
        .size = 0};
    if (ax25_addr_pack(&packet->destination, &addr_buf))
        return -AX25_ADDR_PACK_FAILED;
    out_buf->size += addr_buf.size;

    addr_buf.data = &out_buf->data[out_buf->size];
    addr_buf.capacity = out_buf->capacity - out_buf->size;

    ((ax25_packet_t *)packet)->source.last = packet->path_len == 0;
    if (ax25_addr_pack(&packet->source, &addr_buf))
        return -AX25_ADDR_PACK_FAILED;
    out_buf->size += addr_buf.size;

    for (int i = 0; i < packet->path_len; i++)
    {
        addr_buf.data = &out_buf->data[out_buf->size];
        addr_buf.capacity = out_buf->capacity - out_buf->size;

        ((ax25_packet_t *)packet)->path[i].last = (packet->path_len - 1) == i;
        if (ax25_addr_pack(&packet->path[i], &addr_buf))
            return -AX25_ADDR_PACK_FAILED;
        out_buf->size += addr_buf.size;
    }

    out_buf->data[out_buf->size++] = packet->control;
    out_buf->data[out_buf->size++] = packet->protocol;

    memcpy(&out_buf->data[out_buf->size], packet->info, packet->info_len);
    out_buf->size += packet->info_len;

    return AX25_SUCCESS;
}

ax25_error_e ax25_packet_unpack(ax25_packet_t *packet, const buffer_t *buf)
{
    nonnull(packet, "packet");
    assert_buffer_valid(buf);

    if (!buf_has_size_ge(buf, AX25_MIN_PACKET_LEN))
        return -1;

    int buffer_pos = 0;

    buffer_t addr_buf = {
        .data = &buf->data[buffer_pos],
        .capacity = buf->size - buffer_pos,
        .size = AX25_ADDR_LEN};
    if (ax25_addr_unpack(&packet->destination, &addr_buf))
        return -1;
    buffer_pos += addr_buf.size;

    addr_buf.data = &buf->data[buffer_pos];
    addr_buf.capacity = buf->size - buffer_pos;
    if (ax25_addr_unpack(&packet->source, &addr_buf))
        return -1;
    buffer_pos += addr_buf.size;

    packet->path_len = 0;
    ax25_addr_t address;
    bool is_last = packet->source.last;
    while (!is_last && packet->path_len < AX25_MAX_PATH_LEN && buf_has_size_ge(buf, buffer_pos + AX25_ADDR_LEN))
    {
        addr_buf.data = &buf->data[buffer_pos];
        addr_buf.capacity = buf->size - buffer_pos;
        if (ax25_addr_unpack(&address, &addr_buf))
            return -1;
        buffer_pos += addr_buf.size;

        packet->path[packet->path_len++] = address;
        is_last = address.last;
    }

    if (!buf_has_size_ge(buf, buffer_pos + 2)) // Will allow control & protocol fields
        return -1;

    packet->control = buf->data[buffer_pos++];
    packet->protocol = buf->data[buffer_pos++];

    packet->info_len = buf->size - buffer_pos;
    if (packet->info_len > AX25_MAX_INFO_LEN)
    {
        LOG("excessive information field length %d (max %d), limiting to max", packet->info_len, AX25_MAX_INFO_LEN);
        packet->info_len = AX25_MAX_INFO_LEN;
    }
    memcpy(packet->info, &buf->data[buffer_pos], packet->info_len);

    return AX25_SUCCESS;
}
