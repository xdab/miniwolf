#ifndef TEST_DEDUPE_H
#define TEST_DEDUPE_H

#include "test.h"
#include "dedupe.h"

void test_dedupe_init()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    assert_equal_int(dd.expiration, 300, "dedupe init expiration");
    for (int i = 0; i < DD_MAX_REMEMBERED_FRAMES; i++)
    {
        assert_equal_int(dd.frames[i].crc, 0, "dedupe init crc zero");
        assert_equal_int(dd.frames[i].time, 0L, "dedupe init time zero");
    }
}

void test_dedupe_first_frame_not_duplicate()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    bool result = dedupe_push_frame_at(&dd, 1, 1000);
    assert_true(!result, "first frame not duplicate");
    assert_equal_int(dd.frames[0].crc, 1, "first frame crc stored");
    assert_equal_int(dd.frames[0].time, 1000, "first frame time stored");
}

void test_dedupe_duplicate_within_expiration()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 2, 1000);
    bool result = dedupe_push_frame_at(&dd, 2, 1100); // 100 seconds later, within 300
    assert_true(result, "duplicate within expiration");
}

void test_dedupe_duplicate_after_expiration()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 3, 1000);
    bool result = dedupe_push_frame_at(&dd, 3, 1400); // 400 seconds later, expired
    assert_true(!result, "duplicate after expiration not detected");
}

void test_dedupe_different_crc_not_duplicate()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 4, 1000);
    bool result = dedupe_push_frame_at(&dd, 5, 1100);
    assert_true(!result, "different crc not duplicate");
}

void test_dedupe_slot_eviction()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);

    // Fill all slots with unique CRCs at different times
    for (int i = 0; i < DD_MAX_REMEMBERED_FRAMES; i++)
        dedupe_push_frame_at(&dd, 100 + i, (time_t)i * 10);

    // Add one more, should evict the oldest (time 0)
    dedupe_push_frame_at(&dd, 999, 1000);

    // Check that CRC 100 (time 0) is evicted and replaced
    bool found_999 = false;
    bool found_100 = false;
    for (int i = 0; i < DD_MAX_REMEMBERED_FRAMES; i++)
    {
        if (dd.frames[i].crc == 999)
            found_999 = true;
        if (dd.frames[i].crc == 100)
            found_100 = true;
    }
    assert_true(found_999, "new crc stored after eviction");
    assert_true(!found_100, "oldest crc evicted");
}

void test_dedupe_crc_at_all_indices()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);

    // Store CRCs in specific slots
    dedupe_push_frame_at(&dd, 6, 100); // index 0
    dedupe_push_frame_at(&dd, 7, 200); // index 1
    dedupe_push_frame_at(&dd, 8, 300); // index 2

    // Test duplicate at index 0
    bool result = dedupe_push_frame_at(&dd, 6, 150);
    assert_true(result, "duplicate at index 0 detected");
}

void test_dedupe_time_boundary()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 9, 1000);

    // At exact expiration time (1000 + 300 = 1300), should not be duplicate
    bool result = dedupe_push_frame_at(&dd, 9, 1300);
    assert_true(!result, "exact expiration time not duplicate");
}

void test_dedupe_expired_crc_reuse_slot()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 10, 1000);

    // Let it expire (400 seconds later)
    bool result = dedupe_push_frame_at(&dd, 10, 1400);
    assert_true(!result, "expired crc should not be duplicate");

    // Should reuse the same slot
    assert_equal_int(dd.frames[0].crc, 10, "expired crc slot reused");
    assert_equal_int(dd.frames[0].time, 1400, "expired crc slot updated time");
}

void test_dedupe_rapid_same_crc()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 11, 1000);

    // Same CRC multiple times within expiration
    bool result1 = dedupe_push_frame_at(&dd, 11, 1050);
    bool result2 = dedupe_push_frame_at(&dd, 11, 1100);
    bool result3 = dedupe_push_frame_at(&dd, 11, 1150);
    assert_true(result1, "rapid duplicate 1 detected");
    assert_true(result2, "rapid duplicate 2 detected");
    assert_true(result3, "rapid duplicate 3 detected");
}

void test_dedupe_multiple_expirations()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);

    // Fill with different CRCs at different times
    dedupe_push_frame_at(&dd, 12, 1000); // Will expire at 1300
    dedupe_push_frame_at(&dd, 13, 1100); // Will expire at 1400
    dedupe_push_frame_at(&dd, 14, 1200); // Will expire at 1500

    // Test after first expiration time
    bool result = dedupe_push_frame_at(&dd, 12, 1350); // Should not be duplicate
    assert_true(!result, "first expired crc not duplicate");
}

void test_dedupe_timestamp_bumping()
{
    dedupe_t dd;
    dedupe_init(&dd, 300);
    dedupe_push_frame_at(&dd, 15, 1000);

    // First duplicate - should be detected and timestamp bumped
    bool result1 = dedupe_push_frame_at(&dd, 15, 1050);
    assert_true(result1, "first duplicate detected");
    assert_equal_int(dd.frames[0].time, 1050, "timestamp bumped after first duplicate");

    // Second duplicate much later - should still be detected due to timestamp bump
    bool result2 = dedupe_push_frame_at(&dd, 15, 1299); // 1299-1050=249 < 300
    assert_true(result2, "second duplicate detected after timestamp bump");
    assert_equal_int(dd.frames[0].time, 1299, "timestamp bumped again");

    // Third duplicate after expiration - should not be detected
    bool result3 = dedupe_push_frame_at(&dd, 15, 1600); // 1600-1299=301 > 300
    assert_true(!result3, "duplicate not detected after expiration");
}

#endif
