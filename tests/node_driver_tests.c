#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>

#include "host/node_driver.h"
#include "host/ring_buffer.h"
#include "test_harness.h"

/* ===========================================================================
 * Node Driver Tests
 * ========================================================================= */

/**
 * Case:     Transmission (TX): Pushes data bytes into the ring buffer, runs the
 *           node driver process connected via a PTY master/slave pair, and verifies
 *           that the driver correctly pops and writes the expected byte to the TTY.
 * Expected: The master end of the PTY successfully reads the transmitted byte matching
 *           the first element of the test data buffer.
 */
static void test_node_driver_tx(void)
{
    printf("=== RUNNING TEST: Node Driver TX (Ring Buffer -> Dummy TTY) ===\n");

    // 1. Open pseudoterminal master
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master_fd >= 0);
    assert(grantpt(master_fd) == 0);
    assert(unlockpt(master_fd) == 0);

    // 2. Get slave address
    char slave_name[64];
    if (ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0)
    {
        perror("ptsname_r failed");
        exit(EXIT_FAILURE);
    }

    // Initialize ring buffer and insert data
    ring_buffer_t tx_rb;
    ring_buffer_init(&tx_rb);
    uint8_t test_data[] = {0x7E, 0x00, 0x02, 0x0A, 0x00, 0x6B, 0x05};
    for (size_t i = 0; i < sizeof(test_data); i++)
    {
        ring_buffer_push(&tx_rb, &test_data[i], 1);
    }

    // Start child process
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        // --- CHILD PROCESS(sends data) ---
        close(master_fd);
        node_driver_run(0, slave_name, &tx_rb);
        exit(EXIT_SUCCESS);
    }
    else
    {
        // --- FATHER PROCESS(receives data) ---

        // Read received data
        uint8_t received_byte = 0;
        ssize_t n = read(master_fd, &received_byte, 1);

        if (n > 0)
        {
            printf("[Test PTY] ¡Receive from driver: 0x%02X!\n", received_byte);
            assert(received_byte == test_data[0]);
            printf(">>> TEST TX PASSED SUCCESSFULLY!\n\n");
        }
        else
        {
            printf(">>> TEST TX FAILED: No data received.\n\n");
        }

        // Kill node driver process
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);

        close(master_fd);
    }
}

/**
 * Case:     Reception (RX): Simulates incoming bytes from an external node/dummy TTY
 *           by writing a valid protocol frame into the PTY master channel while
 *           the node driver reads and feeds them into its internal parser.
 * Expected: The write operation completes successfully for the full length of the
 *           dummy response frame without errors or crashes.
 */
void test_node_driver_rx(void)
{
    printf("=== RUNNING TEST: Node Driver RX (Dummy TTY -> Parser) ===\n");

    // 1. Open pseudoterminal master
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master_fd >= 0);
    assert(grantpt(master_fd) == 0);
    assert(unlockpt(master_fd) == 0);

    // 2. Get slave address
    char slave_name[64];
    if (ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0)
    {
        perror("ptsname_r failed");
        exit(EXIT_FAILURE);
    }

    // Initialize empty ring buffer
    ring_buffer_t tx_rb;
    ring_buffer_init(&tx_rb);

    // Start child process
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    {
        // --- CHILD PROCESS(receives data) ---
        close(master_fd);
        node_driver_run(0, slave_name, &tx_rb);
        exit(EXIT_SUCCESS);
    }
    else
    {
        // --- FATHER PROCESS(sends data) ---

        // Send a valid frame
        uint8_t dummy_response[] = {0x7E, 0x01, 0x04, 0xE8, 0x03, 0x00, 0x00, 0x12, 0x34};

        ssize_t sent = write(master_fd, dummy_response, sizeof(dummy_response));
        assert(sent == (ssize_t)sizeof(dummy_response));

        // Kill node driver process
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);

        close(master_fd);
    }
}

/* ===========================================================================
 * Main
 * ========================================================================= */
int main(void)
{
    puts("Node Driver");
    RUN_TEST(test_node_driver_tx);
    RUN_TEST(test_node_driver_rx);

    puts("OK: All node driver tests passed!");
    return 0;
}
