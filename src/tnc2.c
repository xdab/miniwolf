#include "tnc2.h"
#include "common.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

int tnc2_addr_to_string(const ax25_addr_t *addr, buffer_t *out_buf)
{
    nonnull(addr, "addr");
    assert_buffer_valid(out_buf);

    int len = 0;
    for (int i = 0; i < AX25_ADDR_MAX_CALLSIGN_LEN; i++)
    {
        char c = addr->callsign[i];
        if (c == AX25_ADDR_PAD)
            break;
        if (!buf_has_capacity_ge(out_buf, len + 1))
            return -1;
        out_buf->data[len++] = c;
    }
    if (addr->ssid)
    {
        int remaining = out_buf->capacity - len;
        int n = snprintf((char *)out_buf->data + len, remaining, "-%d", addr->ssid);
        if (n >= remaining)
            return -1;
        len += n;
    }
    if (addr->repeated)
    {
        if (!buf_has_capacity_ge(out_buf, len + 1))
            return -1;
        out_buf->data[len++] = '*';
    }
    if (buf_has_capacity_ge(out_buf, len + 1))
        out_buf->data[len] = '\0';
    out_buf->size = len;
    return len;
}

int tnc2_string_to_addr(ax25_addr_t *addr, const buffer_t *buf)
{
    nonnull(addr, "addr");
    assert_buffer_valid(buf);

    memset(addr->callsign, AX25_ADDR_PAD, AX25_ADDR_MAX_CALLSIGN_LEN);
    addr->ssid = 0;
    addr->repeated = 0;

    size_t pos = 0;
    int callsign_len = 0;

    while (pos < buf->size && callsign_len < AX25_ADDR_MAX_CALLSIGN_LEN)
    {
        unsigned char c = (unsigned char)buf->data[pos];
        if (c < 32 || c >= 127)
            return -1;
        if (c == '-' || c == '*' || c == ',' || c == '>' || c == ':')
            break;
        if (!isalnum(c))
            return -1;
        addr->callsign[callsign_len++] = c;
        pos++;
    }

    if (callsign_len == 0 || callsign_len > AX25_ADDR_MAX_CALLSIGN_LEN)
        return -1;

    if (pos < buf->size && buf->data[pos] == '-')
    {
        pos++;
        int ssid = 0;
        int has_digit = 0;
        while (pos < buf->size && isdigit((unsigned char)buf->data[pos]))
        {
            int digit = buf->data[pos] - '0';
            if (ssid > (15 - digit) / 10)
                return -1;
            ssid = ssid * 10 + digit;
            has_digit = 1;
            pos++;
        }
        if (!has_digit || ssid > 15)
            return -1;
        addr->ssid = ssid;
    }

    if (pos < buf->size && buf->data[pos] == '*')
    {
        addr->repeated = 1;
        pos++;
    }

    return (int)pos;
}

int tnc2_packet_to_string(const ax25_packet_t *packet, buffer_t *out_buf)
{
    nonnull(packet, "packet");
    assert_buffer_valid(out_buf);

    int pos = 0;

    buffer_t addr_buf = {
        .data = &out_buf->data[pos],
        .capacity = out_buf->capacity - pos,
        .size = 0};
    int n = tnc2_addr_to_string(&packet->source, &addr_buf);
    if (n < 0)
        return -1;
    pos += n;

    if (!buf_has_capacity_ge(out_buf, pos + 1))
        return -1;
    out_buf->data[pos++] = '>';

    for (int i = -1; i < (int)packet->path_len; i++)
    {
        if (i >= 0)
        {
            if (!buf_has_capacity_ge(out_buf, pos + 1))
                return -1;
            out_buf->data[pos++] = ',';
        }
        const ax25_addr_t *addr = (i == -1) ? &packet->destination : &packet->path[i];
        addr_buf.data = &out_buf->data[pos];
        addr_buf.capacity = out_buf->capacity - pos;
        addr_buf.size = 0;
        n = tnc2_addr_to_string(addr, &addr_buf);
        if (n < 0)
            return -1;
        pos += n;
    }

    if (!buf_has_capacity_ge(out_buf, pos + 1))
        return -1;
    out_buf->data[pos++] = ':';

    if (!buf_has_capacity_ge(out_buf, pos + packet->info_len))
        return -1;
    memcpy(&out_buf->data[pos], packet->info, packet->info_len);
    pos += packet->info_len;

    if (buf_has_capacity_ge(out_buf, pos + 1))
        out_buf->data[pos] = '\0';
    out_buf->size = pos;

    return pos;
}

int tnc2_string_to_packet(ax25_packet_t *packet, const buffer_t *buf)
{
    nonnull(packet, "packet");
    assert_buffer_valid(buf);

    ax25_packet_init(packet);

    size_t pos = 0;

    buffer_t addr_buf = {
        .data = &buf->data[pos],
        .capacity = buf->size - pos,
        .size = buf->size - pos};
    int n = tnc2_string_to_addr(&packet->source, &addr_buf);
    if (n <= 0)
        return -1;
    pos += n;

    if (pos >= buf->size || buf->data[pos] != '>')
        return -1;
    pos++;

    addr_buf.data = &buf->data[pos];
    addr_buf.capacity = buf->size - pos;
    addr_buf.size = buf->size - pos;
    n = tnc2_string_to_addr(&packet->destination, &addr_buf);
    if (n <= 0)
        return -1;
    pos += n;

    packet->path_len = 0;

    while (pos < buf->size && buf->data[pos] == ',')
    {
        pos++;
        if (packet->path_len >= AX25_MAX_PATH_LEN)
            return -1;

        addr_buf.data = &buf->data[pos];
        addr_buf.capacity = buf->size - pos;
        addr_buf.size = buf->size - pos;
        n = tnc2_string_to_addr(&packet->path[packet->path_len], &addr_buf);
        if (n <= 0)
            return -1;
        pos += n;
        packet->path_len++;
    }

    if (pos >= buf->size || buf->data[pos] != ':')
        return -1;
    pos++;

    size_t info_len = buf->size - pos;
    if (info_len > AX25_MAX_INFO_LEN)
        return -1;

    if (info_len > 0)
        memcpy(packet->info, &buf->data[pos], info_len);
    packet->info_len = (uint16_t)info_len;

    return 0;
}
