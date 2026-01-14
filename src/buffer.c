#include "buffer.h"

bool buf_has_capacity_ge(const buffer_t *buf, int than)
{
    if (buf == NULL || buf->data == NULL)
        return 0;
    return buf->capacity >= than;
}

bool buf_has_size_ge(const buffer_t *buf, int than)
{
    if (buf == NULL || buf->data == NULL)
        return 0;
    return buf->size >= than;
}

bool fbuf_has_capacity_ge(const float_buffer_t *buf, int than)
{
    if (buf == NULL || buf->data == NULL)
        return 0;
    return buf->capacity >= than;
}

bool fbuf_has_size_ge(const float_buffer_t *buf, int than)
{
    if (buf == NULL || buf->data == NULL)
        return 0;
    return buf->size >= than;
}
