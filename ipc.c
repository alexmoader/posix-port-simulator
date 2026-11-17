#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// В данном модуле находится работа с общей памятью и семафорами.
//
// Внутри:
//  1) создание общей модели и подключение к ней
//  2) семафор защиты и уведомления участников
//  3) освобождение общих объектов после завершения

static void bindParticipant(PortRuntime *runtime, Participant *participant, int id,
                            ParticipantRole role)
{
    participant->runtime = runtime;
    participant->id = id;
    participant->role = role;
    participant->slot = runtime->participantCount;
    runtime->participants[runtime->participantCount++] = participant;
}

void bindParticipants(PortRuntime *runtime)
{
    Port *port = runtime->port;
    runtime->participantCount = 0;
    bindParticipant(runtime, &runtime->dispatcher, 0, ROLE_DISPATCHER);
    for (int i = 0; i < port->shipCount; i++)
        bindParticipant(runtime, &runtime->ships[i], i, ROLE_SHIP);
    for (int i = 0; i < port->berthCount; i++)
        bindParticipant(runtime, &runtime->berths[i], i, ROLE_BERTH);
    for (int i = 0; i < port->tugCount; i++)
        bindParticipant(runtime, &runtime->tugs[i], i, ROLE_TUG);
    for (int i = 0; i < port->craneCount; i++)
        bindParticipant(runtime, &runtime->cranes[i], i, ROLE_CRANE);
    for (int i = 0; i < port->warehouseCount; i++)
        bindParticipant(runtime, &runtime->warehouses[i], i, ROLE_WAREHOUSE);
}

int waitEvent(sem_t *event)
{
    int result;
    do
    {
        result = sem_wait(event);
    } while (result == -1 && errno == EINTR);
    return result == 0;
}

int lockModel(PortRuntime *runtime)
{
    return waitEvent(runtime->stateMutex);
}

void unlockModel(PortRuntime *runtime)
{
    sem_post(runtime->stateMutex);
}

static int mapMemory(PortRuntime *runtime, int create)
{
    int flags = create ? O_CREAT | O_EXCL | O_RDWR : O_RDWR;
    int fd = shm_open(MEMORY_NAME, flags, 0600);
    if (fd == -1)
        return 0;
    if (create)
        runtime->memoryCreated = 1;
    if (create && ftruncate(fd, sizeof(SharedMemory)) == -1)
    {
        close(fd);
        return 0;
    }
    void *memory = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (memory == MAP_FAILED)
        return 0;
    runtime->memory = memory;
    runtime->shared = &runtime->memory->state;
    runtime->port = &runtime->shared->port;
    return 1;
}

static int openEvents(PortRuntime *runtime, int create)
{
    for (int i = 0; i < runtime->participantCount; i++)
    {
        Participant *p = runtime->participants[i];
        if (runtime->mode == IPC_UNNAMED)
        {
            p->wake = &runtime->memory->events[i];
            if (sem_init(p->wake, 1, 0) == -1)
                return 0;
        }
        else
        {
            char name[64];
            snprintf(name, sizeof(name), "/acos42_event_%d", i);
            int flags = create ? O_CREAT | O_EXCL : 0;
            p->wake = sem_open(name, flags, 0600, 0);
            if (p->wake == SEM_FAILED)
                return 0;
        }
        runtime->eventsInitialized++;
    }
    return 1;
}

int createRuntime(PortRuntime *runtime, const Port *port, IpcMode mode)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->owner = 1;
    runtime->mode = mode;
    if (!mapMemory(runtime, 1))
        return 0;
    *runtime->port = *port;
    runtime->shared->mode = mode;
    runtime->shared->ownerPid = getpid();
    runtime->shared->result = 1;
    bindParticipants(runtime);
    for (int i = 0; i < port->warehouseCount; i++)
    {
        runtime->shared->warehouseUpdatedAt[i] = port->currentTime;
        runtime->shared->initialWarehouseLoad += port->warehouses[i].currentLoad;
    }
    if (mode == IPC_UNNAMED)
    {
        if (sem_init(&runtime->memory->mutex, 1, 0) == -1)
            return 0;
        runtime->stateMutex = &runtime->memory->mutex;
    }
    else
    {
        runtime->stateMutex = sem_open(MUTEX_NAME, O_CREAT | O_EXCL, 0600, 0);
        if (runtime->stateMutex == SEM_FAILED)
        {
            runtime->stateMutex = NULL;
            return 0;
        }
    }
    if (!openEvents(runtime, 1))
        return 0;
    // До этого момента независимые участники ждут готовности модели на семафоре.
    unlockModel(runtime);
    return 1;
}

int connectRuntime(PortRuntime *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    runtime->mode = IPC_INDEPENDENT;
    runtime->stateMutex = sem_open(MUTEX_NAME, 0);
    if (runtime->stateMutex == SEM_FAILED)
    {
        runtime->stateMutex = NULL;
        return 0;
    }
    if (!lockModel(runtime))
        return 0;
    int ok = mapMemory(runtime, 0);
    if (ok)
    {
        bindParticipants(runtime);
        ok = runtime->shared->mode == IPC_INDEPENDENT && openEvents(runtime, 0);
    }
    unlockModel(runtime);
    return ok;
}

void notifyParticipant(Participant *participant)
{
    int *pending = &participant->runtime->shared->pending[participant->slot];
    if (!*pending)
    {
        *pending = 1;
        sem_post(participant->wake);
    }
}

void closeRuntime(PortRuntime *runtime)
{
    for (int i = 0; i < runtime->eventsInitialized; i++)
    {
        sem_t *event = runtime->participants[i]->wake;
        if (runtime->mode == IPC_UNNAMED)
        {
            if (runtime->owner)
                sem_destroy(event);
        }
        else
        {
            sem_close(event);
            if (runtime->owner)
            {
                char name[64];
                snprintf(name, sizeof(name), "/acos42_event_%d", i);
                sem_unlink(name);
            }
        }
    }
    if (runtime->stateMutex != NULL)
    {
        if (runtime->mode == IPC_UNNAMED)
        {
            if (runtime->owner)
                sem_destroy(runtime->stateMutex);
        }
        else
        {
            sem_close(runtime->stateMutex);
            if (runtime->owner)
                sem_unlink(MUTEX_NAME);
        }
    }
    if (runtime->memory != NULL)
        munmap(runtime->memory, sizeof(SharedMemory));
    if (runtime->owner && runtime->memoryCreated)
        shm_unlink(MEMORY_NAME);
}
