#pragma once

#include "common.h"
#include <stdbool.h>

typedef struct buffer
{
    unsigned char *data;
    int capacity;
    int size;
} buffer_t;

bool buf_has_capacity_ge(const buffer_t *buf, int than);

bool buf_has_size_ge(const buffer_t *buf, int than);

typedef struct float_buffer
{
    float *data;
    int capacity;
    int size;
} float_buffer_t;

bool fbuf_has_capacity_ge(const float_buffer_t *buf, int than);

bool fbuf_has_size_ge(const float_buffer_t *buf, int than);

#define assert_buffer_valid(buf)                                            \
    {                                                                       \
        nonnull(buf, "buf");                                                \
        nonnull((buf)->data, "buf.data");                                   \
        _assert((buf)->size <= (buf)->capacity, "buf.size <= buf.capacity"); \
    }
