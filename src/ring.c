#include "ring.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ring_buffer
{
    float *buffer;
    size_t size;
    _Atomic size_t read_idx;
    _Atomic size_t write_idx;
};

ring_error_t ring_init(ring_buffer_t **ring, size_t buffer_samples)
{
    nonnull(ring, "ring");
    nonzero(buffer_samples, "buffer_samples");

    if (!ring)
        return RING_ERR_INVALID_ARG;

    *ring = malloc(sizeof(ring_buffer_t));
    if (!*ring)
        return RING_ERR_MEM_BUFFER;

    (*ring)->buffer = malloc(buffer_samples * sizeof(float));
    if (!(*ring)->buffer)
    {
        free(*ring);
        *ring = NULL;
        return RING_ERR_MEM_BUFFER;
    }

    memset((*ring)->buffer, 0, buffer_samples * sizeof(float));

    (*ring)->size = buffer_samples;
    atomic_store(&(*ring)->read_idx, 0);
    atomic_store(&(*ring)->write_idx, 0);

    return RING_SUCCESS;
}

void ring_destroy(ring_buffer_t *ring)
{
    nonnull(ring, "ring");

    if (!ring)
        return;
    free(ring->buffer);
    free(ring);
}

size_t ring_available(const ring_buffer_t *ring)
{
    nonnull(ring, "ring");

    size_t write = atomic_load(&ring->write_idx);
    size_t read = atomic_load(&ring->read_idx);
    return write - read;
}

size_t ring_write(ring_buffer_t *ring, const float *samples, size_t num_samples)
{
    nonnull(ring, "ring");
    nonnull(samples, "samples");

    size_t write = atomic_load(&ring->write_idx);
    size_t read = atomic_load(&ring->read_idx);
    size_t available_space = ring->size - (write - read);

    size_t to_write = (num_samples < available_space) ? num_samples : available_space;

    if (to_write > 0)
    {
        size_t wpos = write % ring->size;
        size_t space_to_end = ring->size - wpos;
        size_t first_chunk = (to_write < space_to_end) ? to_write : space_to_end;
        memcpy(&ring->buffer[wpos], samples, first_chunk * sizeof(float));
        if (to_write > first_chunk)
        {
            size_t second_chunk = to_write - first_chunk;
            memcpy(ring->buffer, samples + first_chunk, second_chunk * sizeof(float));
        }
    }

    atomic_store(&ring->write_idx, write + to_write);
    return to_write;
}

size_t ring_read(ring_buffer_t *ring, float *samples, size_t num_samples)
{
    nonnull(ring, "ring");
    nonnull(samples, "samples");

    size_t write = atomic_load(&ring->write_idx);
    size_t read = atomic_load(&ring->read_idx);
    size_t available = write - read;

    size_t to_read = (num_samples < available) ? num_samples : available;

    if (to_read > 0)
    {
        size_t rpos = read % ring->size;
        size_t space_to_end = ring->size - rpos;
        size_t first_chunk = (to_read < space_to_end) ? to_read : space_to_end;
        memcpy(samples, &ring->buffer[rpos], first_chunk * sizeof(float));
        if (to_read > first_chunk)
        {
            size_t second_chunk = to_read - first_chunk;
            memcpy(samples + first_chunk, ring->buffer, second_chunk * sizeof(float));
        }
    }

    atomic_store(&ring->read_idx, read + to_read);
    return to_read;
}

ring_error_t ring_simple_init(ring_simple_t *ring, size_t capacity)
{
    nonnull(ring, "ring");
    nonzero(capacity, "capacity");

    if (!ring)
        return RING_ERR_INVALID_ARG;

    ring->buffer = calloc(capacity, sizeof(float));
    if (!ring->buffer)
        return RING_ERR_MEM_BUFFER;

    ring->size = capacity;
    ring->head = 0;

    return RING_SUCCESS;
}

float ring_simple_shift1(ring_simple_t *ring, const float sample)
{
    nonnull(ring, "ring");

    if (ring->size == 0)
        return 0.0f;

    float old = ring->buffer[ring->head];
    ring->buffer[ring->head] = sample;
    ring->head = (ring->head + 1) % ring->size;
    return old;
}
