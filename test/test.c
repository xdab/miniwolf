#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "common.h"

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static char current_module_name[256];
static int current_module_total = 0;
static int current_module_passed = 0;

void begin_suite()
{
    _log_level = LOG_LEVEL_STANDARD;
    total_tests = 0;
    passed_tests = 0;
    failed_tests = 0;
    printf("Starting tests...\n");
}

void begin_module(const char *name)
{
    strcpy(current_module_name, name);
    current_module_total = 0;
    current_module_passed = 0;
    printf("Module %s\n", name);
}

void end_module()
{
    printf("Module %s completed: %d/%d passed\n", current_module_name, current_module_passed, current_module_total);
}

int end_suite()
{
    printf("\nTests completed: %d total, %d passed, %d failed\n", total_tests, passed_tests, failed_tests);

    if (failed_tests > 0)
        printf("FAILED\n");
    else
        printf("PASSED\n");

    return failed_tests;
}

static void test_pass()
{
    total_tests++;
    passed_tests++;
    current_module_total++;
    current_module_passed++;
}

static void test_fail(const char *fmt, ...)
{
    total_tests++;
    failed_tests++;
    current_module_total++;
    printf("FAIL - ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void assert_equal_int(int actual, int expected, const char *msg)
{
    if (actual == expected)
        test_pass();
    else
        test_fail("assert equal int %s: expected %d, got %d", msg, expected, actual);
}

void assert_equal_float(float actual, float expected, const char *msg)
{
    float delta = fabsf(actual - expected);
    if (delta < 1e-6f)
        test_pass();
    else
        test_fail("assert equal float %s: expected %f, got %f", msg, expected, actual);
}

void assert_true(int cond, const char *msg)
{
    if (cond)
        test_pass();
    else
        test_fail("assert true %s", msg);
}

void assert_memory(const void *ptr1, const void *ptr2, size_t length, const char *msg)
{
    if (memcmp(ptr1, ptr2, length) == 0)
        test_pass();
    else
        test_fail("assert memory %s", msg);
}

void assert_string(const char *str1, const char *str2, const char *msg)
{
    if (strcmp(str1, str2) == 0)
        test_pass();
    else
        test_fail("assert string %s: expected '%s', got '%s'", msg, str2, str1);
}
