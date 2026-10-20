#pragma once

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

/** Maximum number of child processes managed by this module. */
#define PROCESS_MANAGER_MAX_PROCESSES 16

/**
 * @brief Function executed by a managed child process.
 * @param arg Argument passed to the child function.
 */
typedef void (*process_entry_t)(void *arg);

/**
 * @brief Stores the process manager state.
 */
typedef struct
{
    /** Process IDs of the managed children. */
    pid_t pids[PROCESS_MANAGER_MAX_PROCESSES];
    /** Number of managed children. */
    size_t count;
    /** Nonzero when shutdown has been requested. */
    volatile sig_atomic_t shutdown_requested;
} process_manager_t;

/**
 * @brief Initializes a process manager.
 * @param manager Process manager to initialize.
 * @return 0 on success, or -1 on error.
 */
int process_manager_init(process_manager_t *manager);

/**
 * @brief Installs signal handlers for graceful shutdown.
 * @param manager Process manager that receives shutdown requests.
 * @return 0 on success, or -1 on error.
 */
int process_manager_install_signal_handlers(process_manager_t *manager);

/**
 * @brief Creates and registers a child process.
 * @param manager Process manager that tracks the child.
 * @param entry Function to execute in the child.
 * @param arg Argument passed to the child function.
 * @return Child process ID in the parent, or -1 on error.
 */
pid_t process_manager_create(process_manager_t *manager,
                             process_entry_t entry,
                             void *arg);

/**
 * @brief Waits for managed children to exit.
 * @param manager Process manager tracking the children.
 * @return EXIT_SUCCESS if shutdown completes normally, otherwise EXIT_FAILURE.
 */
int process_manager_wait(process_manager_t *manager);

/**
 * @brief Requests shutdown and sends termination signals to managed children.
 * @param manager Process manager tracking the children.
 */
void process_manager_stop(process_manager_t *manager);

/**
 * @brief Checks whether shutdown has been requested.
 * @param manager Process manager to query.
 * @return Nonzero if shutdown was requested; otherwise 0.
 */
int process_manager_shutdown_requested(const process_manager_t *manager);
