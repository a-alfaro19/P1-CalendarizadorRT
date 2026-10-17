#include "host/ring_buffer.h"

void ring_buffer_init(ring_buffer_t *rb)
{
    if (rb == NULL)
        return;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    return rb->count == 0;
}

size_t ring_buffer_available(const ring_buffer_t *rb)
{
    return rb->count;
}

bool ring_buffer_push(ring_buffer_t *rb, const uint8_t *data, size_t len)
{
    if (rb == NULL || data == NULL)
        return false;

    // Check for space
    if (rb->count + len > RING_BUFFER_SIZE)
    {
        return false;
    }

    for (size_t i = 0; i < len; i++)
    {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    }

    rb->count += len;
    return true;
}

bool ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte)
{
    if (rb == NULL || byte == NULL)
        return false;

    if (ring_buffer_is_empty(rb))
    {
        return false;
    }

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    rb->count--;

    return true;
}
