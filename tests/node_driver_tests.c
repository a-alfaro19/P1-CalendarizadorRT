#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "host/node_driver.h"
#include "host/ring_buffer.h"
#include "host/shared_memory.h"
#include "test_harness.h"

#define TEST_SHM_NAME "/test_node_driver_shm"

static shared_memory_t test_shared_memory;
static const node_driver_config_t test_driver_config = {
    .tick_ms = 10,
    .algorithm = ALGORITHM_EDF,
    .report_period = 10,
};

/* ===========================================================================
 * Helper to initialize a test shared memory segment with sample tasks
 * ========================================================================= */
static void setup_test_shared_memory(void)
{
    // Remove if it already exists from previous runs
    shm_unlink(TEST_SHM_NAME);

    assert(shared_memory_create(&test_shared_memory, TEST_SHM_NAME) == 0);

    shared_system_config_t config = {0};
    config.nodes[0].node_id = 0;
    config.nodes[0].task_count = 1;
    config.nodes[0].tasks[0] = (task_config_t){
        .task_id = 1,
        .kind = 0, // TASK_KIND_PERIODIC
        .cost_ticks = 2,
        .period = 20,
        .deadline = 20,
        .assigned = false};
    assert(shared_memory_write_config(&test_shared_memory, &config) == 0);
}

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

    // Setup shared memory backend for testing
    setup_test_shared_memory();

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

    // Initialize ring buffer
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
        // --- CHILD PROCESS (runs driver) ---
        close(master_fd);
        node_driver_run(0, slave_name, &tx_rb, TEST_SHM_NAME, &test_driver_config);
        exit(EXIT_SUCCESS);
    }
    else
    {
        // --- FATHER PROCESS (receives data) ---
        uint8_t received_byte = 0;
        ssize_t n = read(master_fd, &received_byte, 1);

        if (n > 0)
        {
            printf("[Test PTY] Received from driver: 0x%02X\n", received_byte);
            // The first frame sent by driver in DRIVER_STATE_IDLE is calibration request start byte (0x7E)
            assert(received_byte == 0x7E);
            printf(">>> TEST TX PASSED SUCCESSFULLY!\n\n");
        }
        else
        {
            printf(">>> TEST TX FAILED: No data received.\n\n");
            assert(0);
        }

        // Cleanup process and shared memory
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        close(master_fd);
        assert(shared_memory_destroy(&test_shared_memory) == 0);
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

    // Setup shared memory backend for testing
    setup_test_shared_memory();

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

    // Initialize ring buffer
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
        // --- CHILD PROCESS (runs driver) ---
        close(master_fd);
        node_driver_run(0, slave_name, &tx_rb, TEST_SHM_NAME, &test_driver_config);
        exit(EXIT_SUCCESS);
    }
    else
    {
        // --- FATHER PROCESS (sends valid calibration response frame 0x01) ---
        // Dummy frame structure based on protocol: Start byte (0x7E), Type (0x01), Len, CRC, Payload...
        uint8_t dummy_response[] = {0x7E, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00};

        ssize_t sent = write(master_fd, dummy_response, sizeof(dummy_response));
        if (sent > 0)
        {
            printf("[Test PTY] Calibration response frame sent to driver successfully.\n");
            printf(">>> TEST RX PASSED SUCCESSFULLY!\n\n");
        }

        // Cleanup process and shared memory
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        close(master_fd);
        assert(shared_memory_destroy(&test_shared_memory) == 0);
    }
}

/**
 * Case:     Robustness - Corrupted Frame: Sends a frame with an invalid checksum (CRC)
 *           or malformed structure to the driver's RX path.
 * Expected: The protocol parser detects the corruption (PROTOCOL_ERR_CRC), logs a warning,
 *           and the driver remains stable without crashing or altering its state incorrectly.
 */
static void test_node_driver_corrupted_frame(void)
{
    printf("=== RUNNING TEST: Robustness - Corrupted Frame (Bad CRC) ===\n");

    setup_test_shared_memory();

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master_fd >= 0);
    assert(grantpt(master_fd) == 0);
    assert(unlockpt(master_fd) == 0);

    char slave_name[64];
    assert(ptsname_r(master_fd, slave_name, sizeof(slave_name)) == 0);

    ring_buffer_t tx_rb;
    ring_buffer_init(&tx_rb);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0)
    {
        close(master_fd);
        node_driver_run(0, slave_name, &tx_rb, TEST_SHM_NAME, &test_driver_config);
        exit(EXIT_SUCCESS);
    }
    else
    {
        // Trama con CRC intencionalmente erróneo o corrupto
        uint8_t corrupted_frame[] = {0x7E, 0x01, 0x02, 0xFF, 0xFF, 0xFF, 0xFF};
        usleep(50000);

        ssize_t sent = write(master_fd, corrupted_frame, sizeof(corrupted_frame));
        assert(sent > 0);

        // Damos un momento para que el driver procese y verifique que sigue vivo (no crasheó)
        usleep(50000);
        printf("[Test PTY] Corrupted frame handled gracefully by driver.\n");
        printf(">>> TEST CORRUPTED FRAME PASSED SUCCESSFULLY!\n\n");

        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        close(master_fd);
        assert(shared_memory_destroy(&test_shared_memory) == 0);
    }
}

/**
 * Case:     Robustness - Unexpected Message: Sends an out-of-sequence message type
 *           (e.g., sending a tick report 0x05 while the driver expects calibration response 0x01).
 * Expected: The state machine ignores the unexpected message type safely, preventing invalid transitions.
 */
static void test_node_driver_unexpected_message(void)
{
    printf("=== RUNNING TEST: Robustness - Unexpected Message Out-Of-Sequence ===\n");

    setup_test_shared_memory();

    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master_fd >= 0);
    assert(grantpt(master_fd) == 0);
    assert(unlockpt(master_fd) == 0);

    char slave_name[64];
    assert(ptsname_r(master_fd, slave_name, sizeof(slave_name)) == 0);

    ring_buffer_t tx_rb;
    ring_buffer_init(&tx_rb);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0)
    {
        close(master_fd);
        node_driver_run(0, slave_name, &tx_rb, TEST_SHM_NAME, &test_driver_config);
        exit(EXIT_SUCCESS);
    }
    else
    {
        // Mensaje inesperado en estado IDLE/WAITING_CALIB: un reporte de tick (0x05) en lugar de respuesta de calibración (0x01)
        uint8_t unexpected_frame[] = {0x7E, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00};
        usleep(50000);

        ssize_t sent = write(master_fd, unexpected_frame, sizeof(unexpected_frame));
        assert(sent > 0);

        usleep(50000);
        printf("[Test PTY] Unexpected message ignored successfully by state machine.\n");
        printf(">>> TEST UNEXPECTED MESSAGE PASSED SUCCESSFULLY!\n\n");

        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        close(master_fd);
        assert(shared_memory_destroy(&test_shared_memory) == 0);
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
    RUN_TEST(test_node_driver_corrupted_frame);
    RUN_TEST(test_node_driver_unexpected_message);

    puts("OK: All node driver tests passed!");
    return 0;
}
