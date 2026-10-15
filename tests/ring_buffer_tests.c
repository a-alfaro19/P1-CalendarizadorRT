#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "host/ring_buffer.h"
#include "test_harness.h"

/* ===========================================================================
 * Ring Buffer Tests
 * ========================================================================= */

/**
 * Case:     Initialize a ring buffer, push a single byte, verify state,
 *           pop the byte, and verify the buffer returns to empty.
 * Expected: Buffer starts empty (is_empty = true, available = 0), push succeeds,
 *           pop retrieves the exact byte (0x55), and buffer is empty again.
 */
static void test_ring_buffer_basic(void)
{
    ring_buffer_t rb;
    ring_buffer_init(&rb);

    // Buffer must be empty
    assert(ring_buffer_is_empty(&rb) == true);
    assert(ring_buffer_available(&rb) == 0);

    // Insert element
    uint8_t val_in = 0x55;
    bool pushed = ring_buffer_push(&rb, &val_in, 1);
    assert(pushed == true);
    assert(ring_buffer_is_empty(&rb) == false);

    // Extract element
    uint8_t val_out = 0;
    bool popped = ring_buffer_pop(&rb, &val_out);
    assert(popped == true);
    assert(val_out == 0x55);

    // Buffer must be empty after pop()
    assert(ring_buffer_is_empty(&rb) == true);
}

/**
 * Case:     Push multiple sequential bytes into the ring buffer and extract them
 *           successively to check ordering and circular buffer behavior.
 * Expected: All pushes succeed, bytes are popped in strict FIFO order (First In, First Out),
 *           and the buffer is fully empty at the end.
 */
static void test_ring_buffer_fifo_order(void)
{
    ring_buffer_t rb;
    ring_buffer_init(&rb);

    // Insert multiple sequential bytes
    uint8_t data_in[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    for (size_t i = 0; i < sizeof(data_in); i++)
    {
        assert(ring_buffer_push(&rb, &data_in[i], 1) == true);
    }

    // Extract and verify bytes order (FIFO: First In, First Out)
    for (size_t i = 0; i < sizeof(data_in); i++)
    {
        uint8_t val_out = 0;
        assert(ring_buffer_pop(&rb, &val_out) == true);
        assert(val_out == data_in[i]);
    }

    // Buffer must be empty
    assert(ring_buffer_is_empty(&rb) == true);
}

/* ===========================================================================
 * Main
 * ========================================================================= */
int main(void)
{
    puts("Ring Buffer");
    RUN_TEST(test_ring_buffer_basic);
    RUN_TEST(test_ring_buffer_fifo_order);

    puts("OK: All ring buffer tests passed!");
    return 0;
}
