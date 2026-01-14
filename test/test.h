#ifndef TEST_H
#define TEST_H

#include <stddef.h>

void begin_suite();
void begin_module(const char *name);
void end_module();
int end_suite();

void assert_equal_int(int actual, int expected, const char *msg);
void assert_equal_float(float actual, float expected, const char *msg);
void assert_true(int cond, const char *msg);
void assert_memory(const void *ptr1, const void *ptr2, size_t length, const char *msg);
void assert_string(const char *str1, const char *str2, const char *msg);

#endif
