#include "host/shared_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define SHARED_MEMORY_MAGIC 0x53484D31u

static int valid_memory(const shared_memory_t *memory)
{
    if (memory == NULL || memory->data == NULL ||
        memory->data->magic != SHARED_MEMORY_MAGIC)
    {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

static int lock_memory(shared_memory_t *memory)
{
    while (sem_wait(&memory->data->mutex) < 0)
    {
        if (errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static int unlock_memory(shared_memory_t *memory)
{
    return sem_post(&memory->data->mutex);
}

static int copy_name(shared_memory_t *memory, const char *name)
{
    if (memory == NULL || name == NULL || name[0] != '/' ||
        strlen(name) >= sizeof(memory->name))
    {
        errno = EINVAL;
        return -1;
    }

    memset(memory, 0, sizeof(*memory));
    memory->fd = -1;
    memcpy(memory->name, name, strlen(name) + 1);
    memory->size = sizeof(shared_memory_data_t);
    return 0;
}

int shared_memory_create(shared_memory_t *memory, const char *name)
{
    if (copy_name(memory, name) != 0)
        return -1;

    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0)
        return -1;

    if (ftruncate(fd, (off_t)memory->size) < 0)
    {
        int saved_errno = errno;
        close(fd);
        shm_unlink(name);
        errno = saved_errno;
        return -1;
    }

    shared_memory_data_t *data = mmap(NULL,
                                      memory->size,
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED,
                                      fd,
                                      0);
    if (data == MAP_FAILED)
    {
        int saved_errno = errno;
        close(fd);
        shm_unlink(name);
        errno = saved_errno;
        return -1;
    }

    memset(data, 0, memory->size);
    if (sem_init(&data->mutex, 1, 1) < 0)
    {
        int saved_errno = errno;
        munmap(data, memory->size);
        close(fd);
        shm_unlink(name);
        errno = saved_errno;
        return -1;
    }

    for (int i = 0; i < SHARED_NUM_NODES; i++)
        data->config.nodes[i].node_id = -1;
    for (int i = 0; i < SHARED_NUM_NODES; i++)
        ring_buffer_init(&data->tx_ring_buffers[i]);

    data->magic = SHARED_MEMORY_MAGIC;
    memory->fd = fd;
    memory->owner = true;
    memory->data = data;
    return 0;
}

int shared_memory_open(shared_memory_t *memory, const char *name)
{
    if (copy_name(memory, name) != 0)
        return -1;

    int fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0)
        return -1;

    struct stat info;
    if (fstat(fd, &info) < 0)
    {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if ((size_t)info.st_size != memory->size)
    {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    shared_memory_data_t *data = mmap(NULL,
                                      memory->size,
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED,
                                      fd,
                                      0);
    if (data == MAP_FAILED)
    {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    if (data->magic != SHARED_MEMORY_MAGIC)
    {
        munmap(data, memory->size);
        close(fd);
        errno = EINVAL;
        return -1;
    }

    memory->fd = fd;
    memory->owner = false;
    memory->data = data;
    return 0;
}

int shared_memory_close(shared_memory_t *memory)
{
    if (memory == NULL || memory->data == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    int result = 0;
    int saved_errno = 0;
    if (munmap(memory->data, memory->size) < 0)
    {
        result = -1;
        saved_errno = errno;
    }
    if (memory->fd >= 0 && close(memory->fd) < 0 && result == 0)
    {
        result = -1;
        saved_errno = errno;
    }

    memset(memory, 0, sizeof(*memory));
    memory->fd = -1;
    if (result < 0)
        errno = saved_errno;
    return result;
}

int shared_memory_destroy(shared_memory_t *memory)
{
    if (!valid_memory(memory) || !memory->owner)
    {
        errno = EPERM;
        return -1;
    }

    char name[SHARED_MEMORY_NAME_MAX];
    memcpy(name, memory->name, sizeof(name));

    int result = 0;
    int saved_errno = 0;
    if (sem_destroy(&memory->data->mutex) < 0)
    {
        result = -1;
        saved_errno = errno;
    }
    if (shared_memory_close(memory) < 0 && result == 0)
    {
        result = -1;
        saved_errno = errno;
    }
    if (shm_unlink(name) < 0 && errno != ENOENT && result == 0)
    {
        result = -1;
        saved_errno = errno;
    }

    if (result < 0)
        errno = saved_errno;
    return result;
}

int shared_memory_write_config(shared_memory_t *memory,
                               const shared_system_config_t *config)
{
    if (!valid_memory(memory) || config == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < SHARED_NUM_NODES; i++)
    {
        if (config->nodes[i].task_count < 0 ||
            config->nodes[i].task_count > SHARED_MAX_TASKS_PER_NODE)
        {
            errno = EINVAL;
            return -1;
        }
    }
    if (lock_memory(memory) < 0)
        return -1;

    memory->data->config = *config;

    int result = unlock_memory(memory);
    return result;
}

int shared_memory_read_config(shared_memory_t *memory,
                              shared_system_config_t *config)
{
    if (!valid_memory(memory) || config == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (lock_memory(memory) < 0)
        return -1;

    *config = memory->data->config;

    int result = unlock_memory(memory);
    return result;
}

static int find_node_index(const shared_system_config_t *config, int node_id)
{
    for (int i = 0; i < SHARED_NUM_NODES; i++)
        if (config->nodes[i].node_id == node_id)
            return i;
    return -1;
}

int shared_memory_read_next_task(shared_memory_t *memory,
                                 int node_id,
                                 task_config_t *task)
{
    if (!valid_memory(memory) || task == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (lock_memory(memory) < 0)
        return -1;

    int result = 0;
    int node_index = find_node_index(&memory->data->config, node_id);
    if (node_index < 0)
    {
        errno = ENOENT;
        result = -1;
    }
    else
    {
        node_task_list_t *node = &memory->data->config.nodes[node_index];
        int task_capacity = (int)(sizeof(node->tasks) / sizeof(node->tasks[0]));
        if (node->task_count < 0 || node->task_count > task_capacity)
        {
            errno = EOVERFLOW;
            result = -1;
        }
        else
        {
            for (int i = 0; i < node->task_count; i++)
            {
                if (!node->tasks[i].assigned)
                {
                    *task = node->tasks[i];
                    result = 1;
                    break;
                }
            }
        }
    }

    int unlock_result = unlock_memory(memory);
    if (unlock_result < 0)
        return -1;
    return result;
}

int shared_memory_mark_task_assigned(shared_memory_t *memory,
                                     int node_id,
                                     uint8_t task_id)
{
    if (!valid_memory(memory))
        return -1;
    if (lock_memory(memory) < 0)
        return -1;

    int result = -1;
    int node_index = find_node_index(&memory->data->config, node_id);
    if (node_index < 0)
        errno = ENOENT;
    else
    {
        node_task_list_t *node = &memory->data->config.nodes[node_index];
        int task_capacity = (int)(sizeof(node->tasks) / sizeof(node->tasks[0]));
        if (node->task_count < 0 || node->task_count > task_capacity)
        {
            errno = EOVERFLOW;
            result = -1;
        }
        else
        {
            for (int i = 0; i < node->task_count; i++)
            {
                if (node->tasks[i].task_id == task_id)
                {
                    node->tasks[i].assigned = true;
                    result = 0;
                    break;
                }
            }
            if (result != 0)
                errno = ENOENT;
        }
    }

    int unlock_result = unlock_memory(memory);
    return result == 0 ? unlock_result : result;
}

static int get_node_status(shared_memory_t *memory, int node_id, shared_node_status_t **status)
{
    int node_index = find_node_index(&memory->data->config, node_id);
    if (node_index < 0)
    {
        errno = ENOENT;
        return -1;
    }
    *status = &memory->data->nodes[node_index];
    return 0;
}

int shared_memory_set_node_active(shared_memory_t *memory,
                                  int node_id,
                                  bool active)
{
    if (!valid_memory(memory))
        return -1;
    if (lock_memory(memory) < 0)
        return -1;

    shared_node_status_t *status = NULL;
    int result = get_node_status(memory, node_id, &status);
    if (result == 0)
        status->active = active;

    int unlock_result = unlock_memory(memory);
    return result == 0 ? unlock_result : result;
}

int shared_memory_record_tick_report(shared_memory_t *memory,
                                     int node_id,
                                     const msg_tick_report_t *report)
{
    if (!valid_memory(memory) || report == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (lock_memory(memory) < 0)
        return -1;

    shared_node_status_t *status = NULL;
    int result = get_node_status(memory, node_id, &status);
    if (result == 0)
    {
        status->active = true;
        status->last_tick = report->tick;
        status->bad_frames = report->bad_frames;
        status->missed_reports = 0;
    }

    int unlock_result = unlock_memory(memory);
    return result == 0 ? unlock_result : result;
}

int shared_memory_record_missed_report(shared_memory_t *memory, int node_id)
{
    if (!valid_memory(memory))
        return -1;
    if (lock_memory(memory) < 0)
        return -1;

    shared_node_status_t *status = NULL;
    int result = get_node_status(memory, node_id, &status);
    if (result == 0)
        status->missed_reports++;

    int unlock_result = unlock_memory(memory);
    return result == 0 ? unlock_result : result;
}

int shared_memory_record_event(shared_memory_t *memory,
                               int node_id,
                               const msg_event_t *event)
{
    if (!valid_memory(memory) || event == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (lock_memory(memory) < 0)
        return -1;

    shared_node_status_t *status = NULL;
    int result = get_node_status(memory, node_id, &status);
    if (result == 0)
    {
        status->last_event = *event;
        status->has_last_event = true;
        if (event->event_type == EVENT_COMPLETE)
            status->completed_tasks++;
        else if (event->event_type == EVENT_DEADLINE_MISS)
            status->missed_deadlines++;
        else if (event->event_type == EVENT_SPORADIC_IGNORED)
            status->ignored_sporadic_triggers++;
    }

    int unlock_result = unlock_memory(memory);
    return result == 0 ? unlock_result : result;
}

int shared_memory_read_node_status(shared_memory_t *memory,
                                   int node_id,
                                   shared_node_status_t *status)
{
    if (!valid_memory(memory) || status == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (lock_memory(memory) < 0)
        return -1;

    shared_node_status_t *stored_status = NULL;
    int result = get_node_status(memory, node_id, &stored_status);
    if (result == 0)
        *status = *stored_status;

    int unlock_result = unlock_memory(memory);
    return result == 0 ? unlock_result : result;
}

ring_buffer_t *shared_memory_tx_ring_buffer(shared_memory_t *memory,
                                            int node_id)
{
    if (!valid_memory(memory) || node_id <= 0 || node_id > SHARED_NUM_NODES)
    {
        errno = EINVAL;
        return NULL;
    }
    return &memory->data->tx_ring_buffers[node_id - 1];
}
