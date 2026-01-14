#ifndef TNC2_H
#define TNC2_H

#include <stddef.h>
#include "ax25.h"
#include "buffer.h"

// Convert AX25 address to TNC2 string format
int tnc2_addr_to_string(const ax25_addr_t *addr, buffer_t *out_buf);

// Convert TNC2 address string to AX25 address
int tnc2_string_to_addr(ax25_addr_t *addr, const buffer_t *buf);

// Convert AX25 packet to TNC2 string format
int tnc2_packet_to_string(const ax25_packet_t *packet, buffer_t *out_buf);

// Convert TNC2 packet string to AX25 packet
int tnc2_string_to_packet(ax25_packet_t *packet, const buffer_t *buf);

#endif
