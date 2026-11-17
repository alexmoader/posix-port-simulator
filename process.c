#include "process.h"
#include "dispatcher.h"
#include "ipc.h"
#include "output.h"
#include "resources.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// В данном модуле находится управление процессами.
//
// Внутри:
//  1) запуск потомков или ожидание независимых участников
//  2) общий цикл участника и модельное время
//  3) завершение по условию или сигналу

IpcMode simulationMode = IPC_UNNAMED;
static volatile sig_atomic_t interrupted;
static pid_t ownerPid;

static void handleInterrupt(int signalNumber)
{
    (void)signalNumber;
    int previousError = errno;
    if (getpid() == ownerPid)
        interrupted = 1;
    else
        kill(ownerPid, SIGINT); // сигнал участнику останавливает весь порт
    errno = previousError;
}

static void pauseUs(int delayUs)
{
    struct timespec delay = {delayUs / 1000000, (long)(delayUs % 1000000) * 1000};
    nanosleep(&delay, NULL);
}

void participantStep(Participant *participant)
{
    switch (participant->role)
    {
    case ROLE_DISPATCHER:
        dispatcherStep(participant);
        break;
    case ROLE_SHIP:
        shipStep(participant);
        break;
    case ROLE_BERTH:
        berthStep(participant);
        break;
    case ROLE_TUG:
        tugStep(participant);
        break;
    case ROLE_CRANE:
        craneStep(participant);
        break;
    case ROLE_WAREHOUSE:
        warehouseStep(participant);
        break;
    }
}

static int participantLoop(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    SharedState *state = runtime->shared;
    lockModel(runtime);
    if (state->pids[participant->slot] != 0)
    {
        unlockModel(runtime);
        return 0;
    }
    state->pids[participant->slot] = getpid();
    if (state->stopRequested)
    {
        state->finished[participant->slot] = 1;
        unlockModel(runtime);
        return 1;
    }
    unlockModel(runtime);
    while (waitEvent(participant->wake))
    {
        lockModel(runtime);
        state->pending[participant->slot] = 0;
        int stopping = state->stopRequested;
        // При остановке краны и склады учитывают уже выполненную работу.
        if (!stopping || participant->role == ROLE_CRANE || participant->role == ROLE_WAREHOUSE)
            participantStep(participant);
        if (stopping)
            state->finished[participant->slot] = 1;
        unlockModel(runtime);
        if (stopping)
            return 1;
    }
    return 0;
}

int printWorkers(void)
{
    PortRuntime runtime;
    if (!connectRuntime(&runtime))
    {
        closeRuntime(&runtime);
        return 1;
    }
    const char *names[] = {"dispatcher", "ship", "berth", "tug", "crane", "warehouse"};
    for (int i = 0; i < runtime.participantCount; i++)
    {
        Participant *p = runtime.participants[i];
        printf("%s %d\n", names[p->role], p->id);
    }
    closeRuntime(&runtime);
    return 0;
}

int runWorker(const char *role, int id)
{
    PortRuntime runtime;
    if (!connectRuntime(&runtime))
    {
        fprintf(stderr, "Сначала запусти port_sim_independent\n");
        closeRuntime(&runtime);
        return 1;
    }
    ownerPid = runtime.shared->ownerPid;
    signal(SIGINT, handleInterrupt);
    signal(SIGTERM, handleInterrupt);
    initWorkerLog("port.log");
    const char *names[] = {"dispatcher", "ship", "berth", "tug", "crane", "warehouse"};
    int ok = 0;
    for (int i = 0; i < runtime.participantCount; i++)
    {
        Participant *p = runtime.participants[i];
        if (strcmp(names[p->role], role) == 0 && p->id == id)
        {
            ok = participantLoop(p);
            break;
        }
    }
    closeLog();
    closeRuntime(&runtime);
    return ok ? 0 : 1;
}

static int startParticipants(PortRuntime *runtime)
{
    fflush(NULL);
    for (int i = 0; i < runtime->participantCount; i++)
    {
        Participant *p = runtime->participants[i];
        p->pid = fork();
        if (p->pid == -1)
            return 0;
        if (p->pid == 0)
        {
            runtime->owner = 0;
            int ok = participantLoop(p);
            closeLog();
            closeRuntime(runtime);
            _exit(ok ? 0 : 1);
        }
    }
    return 1;
}

static int allConnected(PortRuntime *runtime)
{
    for (int i = 0; i < runtime->participantCount; i++)
        if (runtime->shared->pids[i] == 0)
            return 0;
    return 1;
}

static void notifyTick(PortRuntime *runtime)
{
    Port *port = runtime->port;
    for (int i = 0; i < port->shipCount; i++)
        if (port->ships[i].state == SHIP_ARRIVED)
            notifyParticipant(&runtime->ships[i]);
    for (int i = 0; i < port->tugCount; i++)
        if (port->tugs[i].busyWithShip != -1)
            notifyParticipant(&runtime->tugs[i]);
    for (int i = 0; i < port->craneCount; i++)
        if (port->cranes[i].busyWithShip != -1)
            notifyParticipant(&runtime->cranes[i]);
    for (int i = 0; i < port->warehouseCount; i++)
        notifyParticipant(&runtime->warehouses[i]);
}

static int finishParticipants(PortRuntime *runtime)
{
    if (runtime->mode == IPC_INDEPENDENT)
    {
        int waiting;
        do
        {
            lockModel(runtime);
            waiting = 0;
            for (int i = 0; i < runtime->participantCount; i++)
                if (runtime->shared->pids[i] != 0 && !runtime->shared->finished[i])
                    waiting = 1;
            unlockModel(runtime);
            if (waiting)
                pauseUs(1000);
        } while (waiting);
        return 1;
    }
    int ok = 1;
    for (int i = 0; i < runtime->participantCount; i++)
    {
        if (runtime->participants[i]->pid <= 0)
            continue;
        int status;
        pid_t result;
        do
        {
            result = waitpid(runtime->participants[i]->pid, &status, 0);
        } while (result == -1 && errno == EINTR);
        if (result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;
    }
    return ok;
}

int runSimulation(Port *port)
{
    if (!validatePort(port))
        return -1;
    ownerPid = getpid();
    interrupted = 0;
    void (*previousInt)(int) = signal(SIGINT, handleInterrupt);
    void (*previousTerm)(int) = signal(SIGTERM, handleInterrupt);
    PortRuntime runtime;
    int result = -1;
    if (!createRuntime(&runtime, port, simulationMode))
    {
        perror("Не удалось создать общую память или семафоры");
        closeRuntime(&runtime);
        signal(SIGINT, previousInt);
        signal(SIGTERM, previousTerm);
        return -1;
    }
    initLog("port.log");
    Port *model = runtime.port;
    if (simulationMode != IPC_INDEPENDENT && !startParticipants(&runtime))
    {
        lockModel(&runtime);
        requestStop(&runtime, -1);
        unlockModel(&runtime);
    }
    lockModel(&runtime);
    // Все участники должны подключиться до первого шага времени.
    while (!allConnected(&runtime) && !interrupted && !runtime.shared->stopRequested)
    {
        unlockModel(&runtime);
        pauseUs(1000);
        lockModel(&runtime);
    }
    logEvent(model, "Начало моделирования");
    notifyTick(&runtime);
    while (!runtime.shared->stopRequested)
    {
        updateStatistics(model);
        int limited = !model->unlimitedMode && model->timeLimit > 0.0;
        if (interrupted)
        {
            logEvent(model, "Моделирование остановлено по сигналу");
            requestStop(&runtime, 1);
        }
        else if (model->shipsDeparted == model->shipCount)
            requestStop(&runtime, 0);
        else if (limited && model->currentTime >= model->timeLimit)
            requestStop(&runtime, 1);
        if (runtime.shared->stopRequested)
            break;
        unlockModel(&runtime);
        pauseUs(model->displayDelayUs > 0 ? model->displayDelayUs : 500);
        lockModel(&runtime);
        double nextTime = model->currentTime + model->timeStep;
        if (limited && nextTime > model->timeLimit)
            nextTime = model->timeLimit;
        if (!isfinite(nextTime) || nextTime <= model->currentTime)
            requestStop(&runtime, -1);
        else if (!interrupted)
        {
            model->currentTime = nextTime;
            notifyTick(&runtime);
        }
    }
    unlockModel(&runtime);
    if (!finishParticipants(&runtime) || !checkInvariants(&runtime))
        runtime.shared->result = -1;
    for (int i = 0; i < model->shipCount; i++)
        releaseAllShipResources(model, &model->ships[i]);
    updateStatistics(model);
    *port = *model; // копируем результат до удаления общей памяти
    result = runtime.shared->result;
    closeRuntime(&runtime);
    signal(SIGINT, previousInt);
    signal(SIGTERM, previousTerm);
    return result;
}
