#pragma once

#include <stdatomic.h>
#include <stddef.h>

typedef enum ring_error
{
    RING_SUCCESS = 0,
    RING_ERR_MEM_BUFFER,  // Failed to allocate buffer
    RING_ERR_INVALID_ARG, // Invalid argument
} ring_error_t;

typedef struct ring_buffer ring_buffer_t;

ring_error_t ring_init(ring_buffer_t **ring, size_t buffer_samples);

void ring_destroy(ring_buffer_t *ring);

size_t ring_available(const ring_buffer_t *ring);

size_t ring_write(ring_buffer_t *ring, const float *samples, size_t num_samples);

size_t ring_read(ring_buffer_t *ring, float *samples, size_t num_samples);

//

typedef struct ring_buffer_simple
{
    float *buffer;
    size_t size;
    size_t head;
} ring_simple_t;

ring_error_t ring_simple_init(ring_simple_t *ring, size_t capacity);

float ring_simple_shift1(ring_simple_t *ring, const float sample);
