#include "host/process_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static process_manager_t *active_manager;

static void handle_shutdown_signal(int signal_number)
{
    (void)signal_number;
    if (active_manager != NULL)
        active_manager->shutdown_requested = 1;
}

static void restore_default_child_signals(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}

int process_manager_init(process_manager_t *manager)
{
    if (manager == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    return 0;
}

int process_manager_install_signal_handlers(process_manager_t *manager)
{
    if (manager == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    active_manager = manager;

    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = handle_shutdown_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0)
    {
        active_manager = NULL;
        return -1;
    }

    return 0;
}

pid_t process_manager_create(process_manager_t *manager,
                             process_entry_t entry,
                             void *arg)
{
    if (manager == NULL || entry == NULL ||
        manager->count >= PROCESS_MANAGER_MAX_PROCESSES)
    {
        errno = EINVAL;
        return -1;
    }

    if (manager->shutdown_requested)
    {
        errno = ECANCELED;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0)
    {
        active_manager = NULL;
        restore_default_child_signals();
        entry(arg);
        _exit(EXIT_SUCCESS);
    }

    manager->pids[manager->count++] = pid;
    return pid;
}

void process_manager_stop(process_manager_t *manager)
{
    if (manager == NULL)
        return;

    manager->shutdown_requested = 1;
    for (size_t i = 0; i < manager->count; i++)
    {
        if (manager->pids[i] > 0)
            kill(manager->pids[i], SIGTERM);
    }
}

int process_manager_wait(process_manager_t *manager)
{
    if (manager == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    size_t remaining = manager->count;
    int exit_status = EXIT_SUCCESS;

    while (remaining > 0 && !manager->shutdown_requested)
    {
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0)
        {
            if (errno == EINTR)
                continue;
            perror("[ProcessManager] Error esperando procesos hijo");
            exit_status = EXIT_FAILURE;
            break;
        }

        for (size_t i = 0; i < manager->count; i++)
        {
            if (manager->pids[i] == pid)
            {
                manager->pids[i] = 0;
                remaining--;
                break;
            }
        }

        fprintf(stderr,
                "[ProcessManager] El proceso hijo %ld terminó inesperadamente.\n",
                (long)pid);
        exit_status = EXIT_FAILURE;
        process_manager_stop(manager);
    }

    if (manager->shutdown_requested || exit_status != EXIT_SUCCESS)
        process_manager_stop(manager);

    for (size_t i = 0; i < manager->count; i++)
    {
        if (manager->pids[i] <= 0)
            continue;

        while (waitpid(manager->pids[i], NULL, 0) < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno != ECHILD)
            {
                perror("[ProcessManager] Error recogiendo proceso hijo");
                exit_status = EXIT_FAILURE;
            }
            break;
        }
        manager->pids[i] = 0;
    }

    active_manager = NULL;
    return exit_status;
}

int process_manager_shutdown_requested(const process_manager_t *manager)
{
    return manager != NULL && manager->shutdown_requested;
}
