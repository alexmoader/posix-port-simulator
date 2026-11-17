#include "dispatcher.h"
#include "runtime.h"
#include "resources.h"
#include "output.h"

#include <math.h>
#include <stddef.h>

// В данном модуле находится реализация диспетчера.
//
// Внутри:
//  1) проверка модели из второго ИДЗ
//  2) назначение ресурсов для ожидающих запросов
//  3) поэтапная стратегия и резервирование набора

int validatePort(const Port *port)
{
    if (port == NULL)
        return 0;
    if (!isfinite(port->currentTime) || port->currentTime < 0.0)
        return 0;
    if (!isfinite(port->timeStep) || port->timeStep <= 0.0)
        return 0;
    if (!isfinite(port->timeLimit) || port->timeLimit < 0.0)
        return 0;
    if (!isfinite(port->enteringTime) || port->enteringTime <= 0.0)
        return 0;
    if (!isfinite(port->leavingTime) || port->leavingTime <= 0.0)
        return 0;
    if (!isfinite(port->warehouseUnloadRate) || port->warehouseUnloadRate <= 0.0)
        return 0;
    if (port->dispatchStrategy != DISPATCH_STAGED && port->dispatchStrategy != DISPATCH_RESERVED)
        return 0;
    if (port->displayDelayUs < 0)
        return 0;

    // проверяем размеры до обращения к массивам
    if (port->berthCount < 0 || port->berthCount > MAX_BERTHS)
        return 0;
    if (port->tugCount < 0 || port->tugCount > MAX_TUGS)
        return 0;
    if (port->craneCount < 0 || port->craneCount > MAX_CRANES)
        return 0;
    if (port->warehouseCount < 0 || port->warehouseCount > MAX_WAREHOUSES)
        return 0;
    if (port->shipCount < 0 || port->shipCount > MAX_SHIPS)
        return 0;

    for (int i = 0; i < port->craneCount; i++)
    {
        double productivity = port->cranes[i].productivity;
        if (!isfinite(productivity) || productivity <= 0.0)
            return 0;
    }

    for (int i = 0; i < port->warehouseCount; i++)
    {
        const Warehouse *w = &port->warehouses[i];
        if (!isfinite(w->capacity) || w->capacity <= 0.0)
            return 0;
        if (!isfinite(w->currentLoad) || w->currentLoad < 0.0 || w->currentLoad > w->capacity)
            return 0;
    }

    for (int i = 0; i < port->shipCount; i++)
    {
        const Ship *s = &port->ships[i];

        // модель начинает работу с нового набора судов
        if (s->id != i || s->state != SHIP_ARRIVED)
            return 0;
        if (s->type < 0 || s->type >= SHIP_TYPE_COUNT)
            return 0;
        if (s->cargo.type < 0 || s->cargo.type >= CARGO_TYPE_COUNT)
            return 0;
        if (!isfinite(s->cargo.volume) || s->cargo.volume < 0.0)
            return 0;
        if (!isfinite(s->arrivalTime) || s->arrivalTime < 0.0)
            return 0;
        if (s->berthId != -1 || s->warehouseId != -1 || s->tugCount != 0 || s->craneCount != 0 ||
            s->cargoProcessed != 0.0)
            return 0;

        // если совместимого ресурса нет, судно не сможет продолжить работу
        if (findFreeBerthForShip(port, s->type) == -1 || findFreeTugForShip(port, s->type) == -1)
            return 0;

        if (s->cargo.volume > CARGO_EPSILON)
        {
            if (findFreeCraneForCargo(port, s->cargo.type) == -1)
                return 0;

            int warehouseFound = 0;
            for (int j = 0; j < port->warehouseCount; j++)
            {
                const Warehouse *w = &port->warehouses[j];
                if (w->compatibleCargoFlags[s->cargo.type] && w->capacity > CARGO_EPSILON)
                {
                    warehouseFound = 1;
                    break;
                }
            }
            if (!warehouseFound)
                return 0;
        }
    }

    return 1;
}

//============================================
// НАЗНАЧЕНИЕ РЕСУРСОВ
//============================================

// Находит совместимый склад со свободным местом для груза судна.
static int findWarehouseWithFreeSpace(PortRuntime *runtime, const Ship *ship)
{
    Port *port = runtime->port;
    for (int i = 0; i < port->warehouseCount; i++)
    {
        Warehouse *w = &port->warehouses[i];
        if (w->compatibleCargoFlags[ship->cargo.type] &&
            w->capacity - w->currentLoad - runtime->shared->warehouseReserved[i] > CARGO_EPSILON)
            return i;
    }
    return -1;
}

// назначает совместимый буксир для входа или выхода судна
static void assignTug(PortRuntime *runtime, Ship *ship, TugMission mission)
{
    Port *port = runtime->port;
    int tugId = findFreeTugForShip(port, ship->type);
    if (tugId == -1)
        return;
    if (mission == TUG_MISSION_ENTERING && port->dispatchStrategy == DISPATCH_RESERVED)
    {
        int berthId = findFreeBerthForShip(port, ship->type);
        if (berthId == -1)
            return;
        occupyBerth(port, berthId, ship->id);
        ship->berthId = berthId;
        logEvent(port, "Судно %d: зарезервирован буксир %d и причал %d", ship->id, tugId, berthId);
    }
    occupyTug(port, tugId, ship->id, mission);
    ship->tugIds[0] = tugId;
    ship->tugCount = 1;
    runtime->shared->tugStartedAt[tugId] = port->currentTime;
    logEvent(port, "Судно %d: назначен буксир %d на %s", ship->id, tugId,
             mission == TUG_MISSION_ENTERING ? "вход" : "выход");
    changeShipState(runtime, ship, mission == TUG_MISSION_ENTERING ? SHIP_ENTERING : SHIP_LEAVING);
    notifyParticipant(&runtime->tugs[tugId]);
}

// Назначает свободный совместимый причал судну.
static void assignBerth(PortRuntime *runtime, Ship *ship)
{
    Port *port = runtime->port;
    if (ship->berthId == -1)
    {
        int berthId = findFreeBerthForShip(port, ship->type);
        if (berthId == -1)
            return;
        occupyBerth(port, berthId, ship->id);
        ship->berthId = berthId;
        logEvent(port, "Судно %d: назначен причал %d", ship->id, berthId);
    }
    notifyParticipant(&runtime->berths[ship->berthId]);
}

// Назначает совместимые свободные краны судну.
static void assignCranes(PortRuntime *runtime, Ship *ship)
{
    Port *port = runtime->port;
    int warehouseId = findWarehouseWithFreeSpace(runtime, ship);
    if (port->dispatchStrategy == DISPATCH_RESERVED && warehouseId == -1)
    {
        if (ship->state != SHIP_WAITING_FOR_WAREHOUSE)
        {
            port->deadlocksPrevented++;
            logEvent(port, "Судно %d: склад заполнен, ожидание места без удержания кранов",
                     ship->id);
            changeShipState(runtime, ship, SHIP_WAITING_FOR_WAREHOUSE);
        }
        return;
    }

    // в staged сначала получаем краны, затем проверяем склад, как в первом ИДЗ
    while (ship->craneCount < MAX_CRANES_PER_SHIP)
    {
        int craneId = findFreeCraneForCargo(port, ship->cargo.type);
        if (craneId == -1)
            break;
        occupyCrane(port, craneId, ship->id);
        ship->craneIds[ship->craneCount++] = craneId;
    }
    if (ship->craneCount == 0)
        return;

    if (warehouseId == -1)
    {
        while (ship->craneCount > 0)
        {
            int id = ship->craneIds[--ship->craneCount];
            ship->craneIds[ship->craneCount] = -1;
            releaseCrane(port, id);
        }
        if (ship->state != SHIP_WAITING_FOR_WAREHOUSE)
        {
            port->deadlocksPrevented++;
            logEvent(port, "Судно %d: склад заполнен, краны освобождены на время ожидания",
                     ship->id);
            changeShipState(runtime, ship, SHIP_WAITING_FOR_WAREHOUSE);
        }
        return;
    }

    Warehouse *warehouse = &port->warehouses[warehouseId];
    ship->warehouseId = warehouseId;
    int assigned = 0;
    int count = ship->craneCount;
    ship->craneCount = 0;
    for (int i = 0; i < count; i++)
    {
        int craneId = ship->craneIds[i];
        Crane *crane = &port->cranes[craneId];
        double remaining =
            ship->cargo.volume - ship->cargoProcessed - runtime->shared->shipPending[ship->id];
        double freeSpace =
            warehouse->capacity - warehouse->currentLoad -
            runtime->shared->warehouseReserved[warehouseId];
        double volume = fmin(remaining, freeSpace);
        if (port->timeStep < volume / crane->productivity)
            volume = port->timeStep * crane->productivity;
        if (volume <= CARGO_EPSILON)
        {
            releaseCrane(port, craneId);
            continue;
        }
        CraneJob *job = &runtime->shared->craneJobs[craneId];
        job->startedAt = port->currentTime;
        job->warehouseId = warehouseId;
        job->volume = volume;
        job->processed = 0.0;
        runtime->shared->shipPending[ship->id] += volume;
        runtime->shared->warehouseReserved[warehouseId] += volume;
        ship->craneIds[assigned++] = craneId;
        ship->craneCount = assigned;
    }
    for (int i = assigned; i < MAX_CRANES_PER_SHIP; i++)
        ship->craneIds[i] = -1;
    if (assigned > 0)
    {
        changeShipState(runtime, ship, SHIP_LOADING);
        for (int i = 0; i < assigned; i++)
        {
            int craneId = ship->craneIds[i];
            logEvent(port, "Судно %d: назначен кран %d, склад %d, резерв %.2f", ship->id, craneId,
                     warehouseId, runtime->shared->craneJobs[craneId].volume);
            notifyParticipant(&runtime->cranes[craneId]);
        }
        logEvent(port, "Судно %d: начало грузовой операции", ship->id);
    }
}

// обрабатывает запросы судов и назначает свободные ресурсы
void dispatcherStep(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    Port *port = runtime->port;
    int visited[MAX_SHIPS] = {0};
    for (int turn = 0; turn < port->shipCount; turn++)
    {
        int selected = -1;
        for (int i = 0; i < port->shipCount; i++)
        {
            if (!visited[i] &&
                (selected == -1 ||
                 runtime->shared->requestOrder[i] < runtime->shared->requestOrder[selected]))
                selected = i;
        }
        visited[selected] = 1;
        Ship *ship = &port->ships[selected];
        switch (ship->state)
        {
        case SHIP_WAITING_FOR_TUG_IN:
            assignTug(runtime, ship, TUG_MISSION_ENTERING);
            break;
        case SHIP_WAITING_FOR_BERTH:
            assignBerth(runtime, ship);
            break;
        case SHIP_WAITING_FOR_CRANE:
        case SHIP_WAITING_FOR_WAREHOUSE:
            assignCranes(runtime, ship);
            break;
        case SHIP_WAITING_FOR_TUG_OUT:
            assignTug(runtime, ship, TUG_MISSION_LEAVING);
            break;
        default:
            break;
        }
    }
    if (!checkInvariants(runtime))
    {
        logEvent(port, "Ошибка: нарушена согласованность ресурсов");
        requestStop(runtime, -1);
    }
}
