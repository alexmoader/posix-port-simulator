#ifndef RUNTIME_H
#define RUNTIME_H

#include "port_types.h"

#include <semaphore.h>
#include <sys/types.h>

// В данном модуле содержатся служебные данные многопроцессной модели.
//
// Внутри:
//  1) структуры участников и заданий кранов
//  2) общая модель без указателей
//  3) локальный контекст процесса
//  4) объявления функций управления моделью

#define CARGO_EPSILON 1e-9
#define MAX_PARTICIPANTS (1 + MAX_SHIPS + MAX_BERTHS + MAX_TUGS + MAX_CRANES + MAX_WAREHOUSES)

typedef enum
{
    ROLE_DISPATCHER,
    ROLE_SHIP,
    ROLE_BERTH,
    ROLE_TUG,
    ROLE_CRANE,
    ROLE_WAREHOUSE
} ParticipantRole;

typedef enum
{
    IPC_UNNAMED,
    IPC_NAMED,
    IPC_INDEPENDENT
} IpcMode;

typedef struct
{
    double startedAt;
    double volume;    // еще не принятая складом часть задания
    double processed; // уже обработанная часть этого задания
    int warehouseId;
} CraneJob;

typedef struct
{
    Port port;
    IpcMode mode;
    pid_t ownerPid;
    int stopRequested;
    int result;
    double initialWarehouseLoad;
    unsigned long nextRequest;
    unsigned long requestOrder[MAX_SHIPS];
    double warehouseReserved[MAX_WAREHOUSES];
    double warehouseUpdatedAt[MAX_WAREHOUSES];
    double shipPending[MAX_SHIPS];
    double tugStartedAt[MAX_TUGS];
    int berthRelease[MAX_BERTHS];
    CraneJob craneJobs[MAX_CRANES];
    int pending[MAX_PARTICIPANTS]; // несколько уведомлений можно объединить в одно
    pid_t pids[MAX_PARTICIPANTS];
    int finished[MAX_PARTICIPANTS];
} SharedState;

typedef struct
{
    SharedState state;
    sem_t mutex;
    sem_t events[MAX_PARTICIPANTS];
} SharedMemory;

typedef struct PortRuntime PortRuntime;

typedef struct
{
    PortRuntime *runtime;
    int id;
    int slot;
    ParticipantRole role;
    sem_t *wake;
    pid_t pid;
} Participant;

struct PortRuntime
{
    Port *port;
    SharedState *shared;
    SharedMemory *memory;
    sem_t *stateMutex; // защищает модель, задания и условия ожидания
    IpcMode mode;
    int owner;
    int memoryCreated;
    int eventsInitialized;
    Participant dispatcher;
    Participant ships[MAX_SHIPS];
    Participant berths[MAX_BERTHS];
    Participant tugs[MAX_TUGS];
    Participant cranes[MAX_CRANES];
    Participant warehouses[MAX_WAREHOUSES];
    Participant *participants[MAX_PARTICIPANTS];
    int participantCount;
};

// Все функции модели ниже вызываются с закрытой общей защитой.
void notifyParticipant(Participant *participant);
void requestStop(PortRuntime *runtime, int result);
void changeShipState(PortRuntime *runtime, Ship *ship, ShipState state);
void updateStatistics(Port *port);
void unloadWarehouse(PortRuntime *runtime, int warehouseId);
int checkInvariants(const PortRuntime *runtime);

// Один шаг поведения участника; ожидание находится в process.c.
void dispatcherStep(Participant *participant);
void shipStep(Participant *participant);
void tugStep(Participant *participant);
void craneStep(Participant *participant);
void berthStep(Participant *participant);
void warehouseStep(Participant *participant);
void participantStep(Participant *participant);

#endif // RUNTIME_H
