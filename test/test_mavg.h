#ifndef TEST_MAVG_H
#define TEST_MAVG_H

#include "test.h"
#include "mavg.h"
#include <math.h>

void test_mavg_init()
{
    mavg_t ma;
    mavg_init(&ma, 5);
    assert_equal_int(ma.capacity, 5, "mavg init capacity");
    assert_equal_int(ma.count, 0, "mavg init count");
    assert_equal_int(ma.index, 0, "mavg init index");
    assert_equal_float(ma.sum, 0.0f, "mavg init sum");
    assert_true(ma.values != NULL, "mavg init values allocated");
    mavg_free(&ma);
}

void test_mavg_update_single()
{
    mavg_t ma;
    mavg_init(&ma, 3);
    float result = mavg_update(&ma, 2.5f);
    assert_equal_float(result, 2.5f, "single value average");
    assert_equal_int(ma.count, 1, "single value count");
    assert_equal_float(ma.sum, 2.5f, "single value sum");
    mavg_free(&ma);
}

void test_mavg_update_multiple()
{
    mavg_t ma;
    mavg_init(&ma, 3);
    mavg_update(&ma, 1.0f);
    mavg_update(&ma, 2.0f);
    float result = mavg_update(&ma, 3.0f);
    assert_equal_float(result, 2.0f, "three values average");
    assert_equal_int(ma.count, 3, "three values count");
    assert_equal_float(ma.sum, 6.0f, "three values sum");
    mavg_free(&ma);
}

void test_mavg_window_wrapping()
{
    mavg_t ma;
    mavg_init(&ma, 3);
    mavg_update(&ma, 1.0f);
    mavg_update(&ma, 2.0f);
    mavg_update(&ma, 3.0f);
    mavg_update(&ma, 4.0f); // Should replace 1.0f
    assert_equal_float(mavg_get(&ma), 3.0f, "wrapping average (2+3+4)/3");
    assert_equal_int(ma.count, 3, "wrapping count");
    assert_equal_float(ma.sum, 9.0f, "wrapping sum");
    mavg_free(&ma);
}

void test_mavg_get_empty()
{
    mavg_t ma;
    mavg_init(&ma, 5);
    assert_equal_float(mavg_get(&ma), 0.0f, "empty buffer average");
    mavg_free(&ma);
}

void test_mavg_get_partial()
{
    mavg_t ma;
    mavg_init(&ma, 5);
    mavg_update(&ma, 1.0f);
    mavg_update(&ma, 2.0f);
    assert_equal_float(mavg_get(&ma), 1.5f, "partial buffer average");
    mavg_free(&ma);
}

void test_mavg_free()
{
    mavg_t ma;
    mavg_init(&ma, 3);
    mavg_update(&ma, 1.0f);
    mavg_free(&ma);
    assert_true(ma.values == NULL, "mavg free values null");
    assert_equal_int(ma.capacity, 0, "mavg free capacity");
    assert_equal_int(ma.count, 0, "mavg free count");
    assert_equal_int(ma.index, 0, "mavg free index");
    assert_equal_float(ma.sum, 0.0f, "mavg free sum");
}

void test_ema_init()
{
    ema_t ema;
    ema_init(&ema, 10);
    assert_true(!isnan(ema.alpha), "ema init alpha not nan");
    assert_true(ema.alpha > 0.0f && ema.alpha < 1.0f, "ema init alpha range");
    assert_true(isnan(ema.current_value), "ema init current value nan");
    ema_free(&ema);
}

void test_ema_update_first()
{
    ema_t ema;
    ema_init(&ema, 10);
    float result = ema_update(&ema, 5.0f);
    assert_equal_float(result, 5.0f, "first ema update equals input");
    assert_equal_float(ema_get(&ema), 5.0f, "first ema get equals input");
    ema_free(&ema);
}

void test_ema_update_standard()
{
    ema_t ema;
    ema_init(&ema, 2); // alpha = 1 - exp(-1/2) ≈ 0.393
    ema_update(&ema, 4.0f);
    float result = ema_update(&ema, 8.0f);
    float expected = 0.393f * 8.0f + (1.0f - 0.393f) * 4.0f; // ≈ 5.536
    assert_true(fabsf(result - expected) < 0.01f, "ema calculation accuracy");
    assert_true(fabsf(ema_get(&ema) - expected) < 0.01f, "ema get accuracy");
    ema_free(&ema);
}

void test_ema_get_uninitialized()
{
    ema_t ema;
    ema_init(&ema, 5);
    assert_equal_float(ema_get(&ema), 0.0f, "uninitialized ema get zero");
    ema_free(&ema);
}

void test_ema_free()
{
    ema_t ema;
    ema_init(&ema, 5);
    ema_update(&ema, 3.0f);
    ema_free(&ema);
    assert_true(isnan(ema.current_value), "ema free current value nan");
    assert_equal_float(ema.alpha, 0.0f, "ema free alpha zero");
}

#endif
