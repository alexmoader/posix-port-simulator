#include "runtime.h"
#include "output.h"
#include "resources.h"

#include <math.h>

// В данном модуле находится поведение участников порта.
//
// Внутри:
//  1) судно запрашивает очередной этап
//  2) буксир выполняет вход или выход
//  3) кран обрабатывает свою порцию, склад вывозит принятый груз
//  4) причал подтверждает швартовку и освобождение

// сообщает о прибытии судна и запрашивает следующий этап
void shipStep(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    Port *port = runtime->port;
    Ship *ship = &port->ships[participant->id];
    if (ship->state == SHIP_ARRIVED && port->currentTime >= ship->arrivalTime)
    {
        logEvent(port, "Прибыло судно %d: %s, груз: %s, объем: %.2f", ship->id,
                 shipTypeName(ship->type), cargoTypeName(ship->cargo.type), ship->cargo.volume);
        logEvent(port, "Судно %d: запрос буксира на вход", ship->id);
        changeShipState(runtime, ship, SHIP_WAITING_FOR_TUG_IN);
    }
    else if (ship->state == SHIP_MOORED)
    {
        if (ship->cargo.volume <= CARGO_EPSILON)
        {
            ship->cargoProcessed = ship->cargo.volume;
            logEvent(port, "Судно %d: груз отсутствует, запрос буксира на выход", ship->id);
            changeShipState(runtime, ship, SHIP_WAITING_FOR_TUG_OUT);
        }
        else
        {
            logEvent(port, "Судно %d: запрос кранов и места на складе", ship->id);
            changeShipState(runtime, ship, SHIP_WAITING_FOR_CRANE);
        }
    }
}

// выполняет вход или выход судна и освобождает буксир
void tugStep(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    Port *port = runtime->port;
    Tug *tug = &port->tugs[participant->id];
    if (tug->busyWithShip == -1)
        return;
    Ship *ship = &port->ships[tug->busyWithShip];
    double duration =
        tug->mission == TUG_MISSION_ENTERING ? port->enteringTime : port->leavingTime;
    if (port->currentTime - runtime->shared->tugStartedAt[tug->id] + CARGO_EPSILON < duration)
        return;
    TugMission mission = tug->mission;
    releaseTug(port, tug->id);
    ship->tugCount = 0;
    ship->tugIds[0] = -1;
    logEvent(port, "Буксир %d: судно %d, %s завершен, буксир освобожден", tug->id, ship->id,
             mission == TUG_MISSION_ENTERING ? "вход" : "выход");
    if (mission == TUG_MISSION_ENTERING)
    {
        changeShipState(runtime, ship, SHIP_WAITING_FOR_BERTH);
        if (ship->berthId != -1)
            notifyParticipant(&runtime->berths[ship->berthId]);
    }
    else
    {
        runtime->shared->berthRelease[ship->berthId] = 1;
        notifyParticipant(&runtime->berths[ship->berthId]);
    }
    notifyParticipant(&runtime->dispatcher);
}

// учитывает выполненную часть задания крана и освобождает ресурсы
static void processCraneJob(PortRuntime *runtime, int craneId)
{
    Port *port = runtime->port;
    Crane *crane = &port->cranes[craneId];
    if (crane->busyWithShip == -1)
        return;
    Ship *ship = &port->ships[crane->busyWithShip];
    CraneJob *job = &runtime->shared->craneJobs[craneId];

    // сначала вывозим старый груз, чтобы не считать новое поступление лежавшим в прошлом
    unloadWarehouse(runtime, job->warehouseId);
    double elapsed = port->currentTime - job->startedAt;
    double volume = job->volume;
    double duration = (job->processed + job->volume) / crane->productivity;
    if (elapsed < duration)
        volume = fmax(0.0, crane->productivity * elapsed - job->processed);
    volume = fmin(volume, job->volume);
    if (volume > 0.0)
    {
        double accepted = occupyWarehouse(port, job->warehouseId, volume);
        job->volume -= accepted;
        job->processed += accepted;
        runtime->shared->warehouseReserved[job->warehouseId] -= accepted;
        runtime->shared->shipPending[ship->id] -= accepted;
        ship->cargoProcessed += accepted;
        logEvent(port, "Кран %d: судно %d, выгружено %.2f, обработано %.2f из %.2f", craneId,
                 ship->id, accepted, ship->cargoProcessed, ship->cargo.volume);
    }
    if (job->volume > CARGO_EPSILON && !runtime->shared->stopRequested)
        return;

    // отмененная часть задания освобождает резерв, но не превращается в обработанный груз
    runtime->shared->warehouseReserved[job->warehouseId] -= job->volume;
    runtime->shared->shipPending[ship->id] -= job->volume;
    job->volume = 0.0;
    releaseCrane(port, craneId);
    for (int i = 0; i < ship->craneCount; i++)
    {
        if (ship->craneIds[i] == craneId)
        {
            ship->craneIds[i] = ship->craneIds[--ship->craneCount];
            ship->craneIds[ship->craneCount] = -1;
            break;
        }
    }
    logEvent(port, "Судно %d: освобожден кран %d", ship->id, craneId);
    if (ship->craneCount == 0 && !runtime->shared->stopRequested)
    {
        runtime->shared->shipPending[ship->id] = 0.0;
        if (ship->cargo.volume - ship->cargoProcessed <= CARGO_EPSILON)
        {
            ship->cargoProcessed = ship->cargo.volume;
            logEvent(port, "Судно %d: грузовая операция завершена, запрос буксира на выход",
                     ship->id);
            changeShipState(runtime, ship, SHIP_WAITING_FOR_TUG_OUT);
        }
        else
            changeShipState(runtime, ship, SHIP_WAITING_FOR_CRANE);
    }
    notifyParticipant(&runtime->dispatcher);
}

// ожидает задания и выполняет работу назначенного крана
void craneStep(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    // при остановке тоже учитываем работу до зафиксированного времени
    processCraneJob(runtime, participant->id);
}

// подтверждает швартовку судна и освобождает причал после выхода
void berthStep(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    Port *port = runtime->port;
    Berth *berth = &port->berths[participant->id];
    if (berth->occupiedByShip == -1)
        return;
    Ship *ship = &port->ships[berth->occupiedByShip];
    if (runtime->shared->berthRelease[berth->id])
    {
        runtime->shared->berthRelease[berth->id] = 0;
        releaseBerth(port, berth->id);
        ship->berthId = -1;
        ship->warehouseId = -1;
        logEvent(port, "Причал %d: освобожден судном %d", berth->id, ship->id);
        changeShipState(runtime, ship, SHIP_DEPARTED);
        logEvent(port, "Судно %d: покинуло порт", ship->id);
    }
    else if (ship->state == SHIP_WAITING_FOR_BERTH)
    {
        logEvent(port, "Причал %d: судно %d пришвартовано", berth->id, ship->id);
        changeShipState(runtime, ship, SHIP_MOORED);
    }
}

// вывозит груз со склада и сообщает об освобождении места
void warehouseStep(Participant *participant)
{
    PortRuntime *runtime = participant->runtime;
    unloadWarehouse(runtime, participant->id);
}
