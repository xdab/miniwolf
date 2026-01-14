#ifndef TEST_RING_H
#define TEST_RING_H

#include "test.h"
#include "ring.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void test_ring_init(void)
{
    ring_buffer_t *ring = NULL;
    assert_equal_int(ring_init(&ring, 4), RING_SUCCESS, "ring_init success");
    assert_equal_int(ring_available(ring), 0, "available 0 after init");
    ring_destroy(ring);
}

void test_ring_write_read(void)
{
    ring_buffer_t *ring = NULL;
    ring_init(&ring, 4);

    float in[] = {1.0f, 2.0f, 3.0f};
    size_t w = ring_write(ring, in, 3);
    assert_equal_int(w, 3, "write 3");
    assert_equal_int(ring_available(ring), 3, "avail 3");

    float out[3];
    size_t r = ring_read(ring, out, 3);
    assert_equal_int(r, 3, "read 3");
    assert_memory(in, out, sizeof(float) * 3, "write read match");

    assert_equal_int(ring_available(ring), 0, "avail 0");

    ring_destroy(ring);
}

void test_ring_full(void)
{
    ring_buffer_t *ring = NULL;
    ring_init(&ring, 4);

    float in[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    size_t w1 = ring_write(ring, in, 5);
    assert_equal_int(w1, 4, "write full 4");
    assert_equal_int(ring_available(ring), 4, "avail full 4");

    ring_destroy(ring);
}

void test_ring_read_empty(void)
{
    ring_buffer_t *ring = NULL;
    ring_init(&ring, 4);

    float out[2];
    size_t r = ring_read(ring, out, 2);
    assert_equal_int(r, 0, "read empty 0");

    ring_destroy(ring);
}

void test_ring_wrap(void)
{
    ring_buffer_t *ring = NULL;
    ring_init(&ring, 4);

    float in1[] = {1.0f, 2.0f, 3.0f, 4.0f};
    ring_write(ring, in1, 4);

    float in2[] = {5.0f, 6.0f};
    size_t w2 = ring_write(ring, in2, 2); // drop 0 when full
    assert_equal_int(w2, 0, "write full drop");

    float out[6];
    size_t rlen = ring_read(ring, out, 6);
    assert_equal_int(rlen, 4, "read full 4");
    float expected[] = {1.0f, 2.0f, 3.0f, 4.0f};
    assert_memory(expected, out, sizeof(float) * 4, "wrap read");

    ring_destroy(ring);
}

void test_ring_shift1_empty(void)
{
    ring_simple_t ring;
    ring_simple_init(&ring, 4);

    float old = ring_simple_shift1(&ring, 1.0f);
    assert_equal_float(old, 0.0f, "shift empty old 0");
}

void test_ring_shift1_delay(void)
{
    ring_simple_t ring;
    ring_simple_init(&ring, 4);

    // Fill buffer with initial values (4 shifts to fill 4-sample buffer)
    float out1 = ring_simple_shift1(&ring, 10.0f); // should return 0.0 (empty buffer)
    assert_equal_float(out1, 0.0f, "shift 1 out 0");

    float out2 = ring_simple_shift1(&ring, 20.0f); // should return 0.0
    assert_equal_float(out2, 0.0f, "shift 2 out 0");

    float out3 = ring_simple_shift1(&ring, 30.0f); // should return 0.0
    assert_equal_float(out3, 0.0f, "shift 3 out 0");

    float out4 = ring_simple_shift1(&ring, 40.0f); // should return 0.0 (buffer not yet full until after this)
    assert_equal_float(out4, 0.0f, "shift 4 out 0");

    // Buffer is now full after 4 samples. Next shift should return oldest (10.0) and make room for new sample
    float out5 = ring_simple_shift1(&ring, 50.0f); // should return 10.0 (oldest)
    assert_equal_float(out5, 10.0f, "shift 5 out oldest 10");

    // Next shift should return next oldest (20.0)
    float out6 = ring_simple_shift1(&ring, 60.0f); // should return 20.0
    assert_equal_float(out6, 20.0f, "shift 6 out oldest 20");
}

#endif
