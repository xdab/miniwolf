#include "common.h"

log_level_e _log_level = LOG_LEVEL_STANDARD;

void byteswap16(uint16_t *v)
{
    *v = (((*v & 0xFF) << 8) | ((*v >> 8) & 0xFF));
}

void byteswap32(uint32_t *v)
{
    *v = (((*v & 0xFF) << 24) | (((*v >> 8) & 0xFF) << 16) |
          (((*v >> 16) & 0xFF) << 8) | ((*v >> 24) & 0xFF));
}

void byteswap64(uint64_t *v)
{
    *v = (((*v & 0xFF) << 56) | (((*v >> 8) & 0xFF) << 48) |
          (((*v >> 16) & 0xFF) << 40) | (((*v >> 24) & 0xFF) << 32) |
          (((*v >> 32) & 0xFF) << 24) | (((*v >> 40) & 0xFF) << 16) |
          (((*v >> 48) & 0xFF) << 8) | ((*v >> 56) & 0xFF));
}