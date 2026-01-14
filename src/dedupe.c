#include "dedupe.h"
#include <limits.h>

void dedupe_init(dedupe_t *dd, time_t expiration_seconds)
{
    dd->expiration = expiration_seconds;
    for (int i = 0; i < DD_MAX_REMEMBERED_FRAMES; i++)
    {
        dd->frames[i].crc = 0;
        dd->frames[i].time = 0L;
    }
}

// Returns true if is duplicate, else false and remembers the frame for future checks
bool dedupe_push_frame(dedupe_t *dd, uint16_t frame_crc)
{
    return dedupe_push_frame_at(dd, frame_crc, time(NULL));
}

// Test-specific version that allows overriding current time
bool dedupe_push_frame_at(dedupe_t *dd, uint16_t frame_crc, time_t current_time)
{
    int earliest_idx = 0;
    time_t earliest_time = LONG_MAX;

    // First pass: look for exact CRC match
    for (int i = 0; i < DD_MAX_REMEMBERED_FRAMES; i++)
    {
        if (dd->frames[i].crc == frame_crc)
        {
            // Found CRC match - always update timestamp first
            time_t old_time = dd->frames[i].time;
            dd->frames[i].time = current_time;

            // Check if it was within expiration from the OLD timestamp
            if (current_time - old_time < dd->expiration)
                return true; // Still a duplicate

            // Expired CRC match - slot reused, not a duplicate
            return false;
        }

        if (dd->frames[i].time < earliest_time)
        {
            earliest_idx = i;
            earliest_time = dd->frames[i].time;
        }
    }

    // No CRC match found - use LRU slot
    dd->frames[earliest_idx].time = current_time;
    dd->frames[earliest_idx].crc = frame_crc;
    return false;
}
