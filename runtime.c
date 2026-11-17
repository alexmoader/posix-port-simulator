#include "runtime.h"
#include "output.h"
#include "resources.h"

#include <math.h>

// В данном модуле реализовано общее управление моделью.
//
// Внутри:
//  1) единый запрос остановки
//  2) уведомление участников об изменении состояния
//  3) учет вывезенного груза
//  4) проверка согласованности модели

void requestStop(PortRuntime *runtime, int result)
{
    if (runtime->shared->stopRequested)
    {
        if (result == -1)
            runtime->shared->result = -1;
        return;
    }
    runtime->shared->result = result;
    runtime->shared->stopRequested = 1;

    // разбудить нужно даже тех, кому за всю работу не назначили ни одного задания
    for (int i = 0; i < runtime->participantCount; i++)
        notifyParticipant(runtime->participants[i]);
}

void changeShipState(PortRuntime *runtime, Ship *ship, ShipState state)
{
    if (ship->state == state)
        return;
    ShipState previous = ship->state;
    ship->state = state;
    ship->stateStartTime = runtime->port->currentTime;
    runtime->shared->requestOrder[ship->id] = ++runtime->shared->nextRequest;
    logEvent(runtime->port, "Судно %d: %s -> %s", ship->id, shipStateName(previous),
             shipStateName(state));
    notifyParticipant(&runtime->ships[ship->id]);
    notifyParticipant(&runtime->dispatcher);
}

void updateStatistics(Port *port)
{
    port->shipsDeparted = 0;
    port->shipsWaiting = 0;

    for (int i = 0; i < port->shipCount; i++)
    {
        const Ship *s = &port->ships[i];

        if (s->state == SHIP_DEPARTED)
        {
            port->shipsDeparted++;
        }
        else if (s->arrivalTime <= port->currentTime &&
                 (s->state == SHIP_ARRIVED || s->state == SHIP_WAITING_FOR_TUG_IN ||
                  s->state == SHIP_WAITING_FOR_BERTH || s->state == SHIP_WAITING_FOR_CRANE ||
                  s->state == SHIP_WAITING_FOR_WAREHOUSE || s->state == SHIP_WAITING_FOR_TUG_OUT))
        {
            // входящие, разгружающиеся и еще не прибывшие суда не ждут
            port->shipsWaiting++;
        }
    }
}

void unloadWarehouse(PortRuntime *runtime, int warehouseId)
{
    Port *port = runtime->port;
    double elapsed = port->currentTime - runtime->shared->warehouseUpdatedAt[warehouseId];
    runtime->shared->warehouseUpdatedAt[warehouseId] = port->currentTime;
    if (elapsed <= 0.0)
        return;
    double load = port->warehouses[warehouseId].currentLoad;
    double volume = load;
    if (elapsed < load / port->warehouseUnloadRate)
        volume = elapsed * port->warehouseUnloadRate;
    double removed = releaseWarehouse(port, warehouseId, volume);
    port->cargoRemoved += removed;
    if (removed > 0.0)
    {
        logEvent(port, "Склад %d: вывезено %.2f, осталось %.2f", warehouseId, removed,
                 port->warehouses[warehouseId].currentLoad);
        notifyParticipant(&runtime->dispatcher);
    }
}

int checkInvariants(const PortRuntime *runtime)
{
    const Port *port = runtime->port;
    double pending[MAX_SHIPS] = {0};
    double reserved[MAX_WAREHOUSES] = {0};
    int cranesSeen[MAX_CRANES] = {0};
    int tugsSeen[MAX_TUGS] = {0};
    double processed = 0.0;
    double stored = 0.0;
    for (int i = 0; i < port->craneCount; i++)
    {
        const Crane *c = &port->cranes[i];
        if (c->busyWithShip == -1)
            continue;
        if (c->busyWithShip < 0 || c->busyWithShip >= port->shipCount)
            return 0;
        const CraneJob *job = &runtime->shared->craneJobs[i];
        if (job->warehouseId < 0 || job->warehouseId >= port->warehouseCount ||
            job->volume < -CARGO_EPSILON)
            return 0;
        if (!port->warehouses[job->warehouseId]
                 .compatibleCargoFlags[port->ships[c->busyWithShip].cargo.type])
            return 0;
        pending[c->busyWithShip] += job->volume;
        reserved[job->warehouseId] += job->volume;
    }
    for (int i = 0; i < port->warehouseCount; i++)
    {
        const Warehouse *w = &port->warehouses[i];
        stored += w->currentLoad;
        if (w->currentLoad < -CARGO_EPSILON ||
            runtime->shared->warehouseReserved[i] < -CARGO_EPSILON ||
            w->currentLoad + runtime->shared->warehouseReserved[i] > w->capacity + 1e-7 ||
            fabs(reserved[i] - runtime->shared->warehouseReserved[i]) > 1e-7)
            return 0;
    }
    for (int i = 0; i < port->shipCount; i++)
    {
        const Ship *s = &port->ships[i];
        processed += s->cargoProcessed;
        if (s->craneCount < 0 || s->craneCount > MAX_CRANES_PER_SHIP || s->tugCount < 0 ||
            s->tugCount > MAX_TUGS_PER_SHIP)
            return 0;
        if (s->cargoProcessed < -CARGO_EPSILON ||
            runtime->shared->shipPending[i] < -CARGO_EPSILON ||
            s->cargoProcessed + runtime->shared->shipPending[i] > s->cargo.volume + 1e-7 ||
            fabs(pending[i] - runtime->shared->shipPending[i]) > 1e-7)
            return 0;
        for (int j = 0; j < s->craneCount; j++)
        {
            int id = s->craneIds[j];
            if (id < 0 || id >= port->craneCount || port->cranes[id].busyWithShip != i ||
                !port->cranes[id].compatibleCargoFlags[s->cargo.type])
                return 0;
            if (++cranesSeen[id] != 1)
                return 0;
        }
        for (int j = 0; j < s->tugCount; j++)
        {
            int id = s->tugIds[j];
            if (id < 0 || id >= port->tugCount || port->tugs[id].busyWithShip != i ||
                !port->tugs[id].compatibleShipFlags[s->type])
                return 0;
            if (++tugsSeen[id] != 1)
                return 0;
        }
        if (s->berthId != -1 && (s->berthId < 0 || s->berthId >= port->berthCount ||
                                 port->berths[s->berthId].occupiedByShip != i ||
                                 !port->berths[s->berthId].compatibleShipFlags[s->type]))
            return 0;
    }
    for (int i = 0; i < port->craneCount; i++)
    {
        if ((port->cranes[i].busyWithShip != -1) != (cranesSeen[i] == 1))
            return 0;
    }
    for (int i = 0; i < port->tugCount; i++)
    {
        if ((port->tugs[i].busyWithShip != -1) != (tugsSeen[i] == 1))
            return 0;
    }
    for (int i = 0; i < port->berthCount; i++)
    {
        int owner = port->berths[i].occupiedByShip;
        if (owner != -1 &&
            (owner < 0 || owner >= port->shipCount || port->ships[owner].berthId != i))
            return 0;
    }
    if (fabs(runtime->shared->initialWarehouseLoad + processed - stored - port->cargoRemoved) >
        1e-7 * fmax(1.0, runtime->shared->initialWarehouseLoad + processed))
        return 0;
    return 1;
}
