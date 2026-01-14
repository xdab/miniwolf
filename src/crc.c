#include "crc.h"
#include "common.h"

void crc_ccitt_init(crc_ccitt_t *crc)
{
    nonnull(crc, "crc");
    
    crc->crc = 0xffff;
}

void crc_ccitt_update(crc_ccitt_t *crc, uint8_t byte)
{
    nonnull(crc, "crc");

    crc->crc = (crc->crc >> 8) ^ CRC_CCITT_TABLE[(crc->crc ^ byte) & 0xff];
}

void crc_ccitt_update_buffer(crc_ccitt_t *crc, const uint8_t *buffer, int length)
{
    nonnull(crc, "crc");
    nonnull(buffer, "buffer");
    nonzero(length, "length");

    for (int i = 0; i < length; i++)
        crc_ccitt_update(crc, buffer[i]);
}

uint16_t crc_ccitt_get(crc_ccitt_t *crc)
{
    nonnull(crc, "crc");

    return crc->crc ^ 0xffff;
}
