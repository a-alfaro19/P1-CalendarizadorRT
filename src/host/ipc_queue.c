#define _POSIX_C_SOURCE 200809L

#include "host/ipc_queue.h"
#include <errno.h>
#include <string.h>
#include <time.h>

int ipc_queue_init(ipc_queue_t *queue)
{
    if (!queue)
    {
        errno = EINVAL;
        return -1;
    }
    memset(queue, 0, sizeof(*queue));
    if (sem_init(&queue->mutex, 1, 1) < 0)
        return -1;
    if (sem_init(&queue->items, 1, 0) < 0)
    {
        int saved = errno;
        sem_destroy(&queue->mutex);
        errno = saved;
        return -1;
    }
    if (sem_init(&queue->spaces, 1, IPC_QUEUE_CAPACITY) < 0)
    {
        int saved = errno;
        sem_destroy(&queue->items);
        sem_destroy(&queue->mutex);
        errno = saved;
        return -1;
    }
    return 0;
}

int ipc_queue_destroy(ipc_queue_t *queue)
{
    if (!queue)
    {
        errno = EINVAL;
        return -1;
    }
    int saved = 0;
    if (sem_destroy(&queue->spaces) < 0)
        saved = errno;
    if (sem_destroy(&queue->items) < 0 && !saved)
        saved = errno;
    if (sem_destroy(&queue->mutex) < 0 && !saved)
        saved = errno;
    if (saved)
    {
        errno = saved;
        return -1;
    }
    return 0;
}

static int wait_sem(sem_t *sem, int timeout_ms, const struct timespec *deadline)
{
    if (timeout_ms < 0)
        return sem_wait(sem);
    if (timeout_ms == 0)
        return sem_trywait(sem);
    return sem_timedwait(sem, deadline);
}

static int transfer(ipc_queue_t *queue, ipc_message_t *output,
                    const ipc_message_t *input, int timeout_ms)
{
    if (!queue || (!output && !input) || timeout_ms < -1)
    {
        errno = EINVAL;
        return -1;
    }
    struct timespec deadline = {0};
    if (timeout_ms > 0)
    {
        if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
            return -1;
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    sem_t *available = input ? &queue->spaces : &queue->items;
    sem_t *ready = input ? &queue->items : &queue->spaces;
    if (wait_sem(available, timeout_ms, &deadline) < 0)
        return -1;
    if (wait_sem(&queue->mutex, timeout_ms, &deadline) < 0)
    {
        int saved = errno;
        sem_post(available); /* Restore reservation on timeout/interruption. */
        errno = saved;
        return -1;
    }
    if (input)
    {
        queue->messages[queue->tail] = *input;
        queue->tail = (queue->tail + 1) % IPC_QUEUE_CAPACITY;
    }
    else
    {
        *output = queue->messages[queue->head];
        queue->head = (queue->head + 1) % IPC_QUEUE_CAPACITY;
    }
    sem_post(&queue->mutex);
    sem_post(ready);
    return 0;
}

int ipc_queue_send(ipc_queue_t *queue, const ipc_message_t *message, int timeout_ms)
{
    if (!message)
    {
        errno = EINVAL;
        return -1;
    }
    return transfer(queue, NULL, message, timeout_ms);
}

int ipc_queue_receive(ipc_queue_t *queue, ipc_message_t *message, int timeout_ms)
{
    if (!message)
    {
        errno = EINVAL;
        return -1;
    }
    return transfer(queue, message, NULL, timeout_ms);
}
