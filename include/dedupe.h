#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define DD_MAX_REMEMBERED_FRAMES 8

struct dedupe_frame
{
    uint16_t crc;
    time_t time;
};

typedef struct dedupe
{
    struct dedupe_frame frames[DD_MAX_REMEMBERED_FRAMES];
    time_t expiration;
} dedupe_t;

void dedupe_init(dedupe_t *dd, time_t expiration_seconds);

// Returns true if is duplicate, else false and remembers the frame for future checks
bool dedupe_push_frame(dedupe_t *dd, uint16_t frame_crc);

// Test-specific version that allows overriding current time
bool dedupe_push_frame_at(dedupe_t *dd, uint16_t frame_crc, time_t now);
