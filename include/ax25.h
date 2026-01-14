#ifndef AX25_H
#define AX25_H

#include "buffer.h"
#include <stdint.h>
#include <stdbool.h>

#define AX25_ADDR_MAX_CALLSIGN_LEN 6
#define AX25_ADDR_LEN 7
#define AX25_ADDR_PAD ' '

typedef enum
{
    AX25_SUCCESS = 0,
    AX25_ADDR_BUF_TOO_SMALL,
    AX25_BUF_TOO_SMALL,
    AX25_ADDR_PACK_FAILED,
} ax25_error_e;

typedef struct ax25_addr
{
    char callsign[AX25_ADDR_MAX_CALLSIGN_LEN];
    int ssid;
    bool repeated;
    bool last;
} ax25_addr_t;

void ax25_addr_init(ax25_addr_t *addr);

void ax25_addr_init_with(ax25_addr_t *addr, const char *callsign, int ssid, bool repeated);

ax25_error_e ax25_addr_pack(const ax25_addr_t *addr, buffer_t *out_buf);

ax25_error_e ax25_addr_unpack(ax25_addr_t *addr, const buffer_t *buf);

#define AX25_CONTROL_LEN 1
#define AX25_PROTOCOL_LEN 1
#define AX25_MAX_PATH_LEN 8
#define AX25_MAX_INFO_LEN 256
#define AX25_MIN_PACKET_LEN (AX25_ADDR_LEN * 2 + AX25_CONTROL_LEN + AX25_PROTOCOL_LEN)
#define AX25_MAX_PACKET_LEN (AX25_MIN_PACKET_LEN + AX25_MAX_PATH_LEN * AX25_ADDR_LEN + AX25_MAX_INFO_LEN)

typedef struct ax25_packet
{
    ax25_addr_t source;
    ax25_addr_t destination;
    ax25_addr_t path[AX25_MAX_PATH_LEN];
    uint8_t path_len;
    uint8_t control;
    uint8_t protocol;
    uint8_t info[AX25_MAX_INFO_LEN];
    uint16_t info_len;
} ax25_packet_t;

void ax25_packet_init(ax25_packet_t *packet);

int ax25_packet_len(const ax25_packet_t *packet);

ax25_error_e ax25_packet_pack(const ax25_packet_t *packet, buffer_t *out_buf);

ax25_error_e ax25_packet_unpack(ax25_packet_t *packet, const buffer_t *buf);

#endif
