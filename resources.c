#include "resources.h"

#include <math.h>
#include <stddef.h>

// В данном модуле находится реализация утилит работы с ресурсами.
//
// Внутри:
//  1) проход по массивам ресурсов в Port
//  2) проверка флагов совместимости (массивы int, 1 = совместим)
//  3) проверка занятости (-1 = свободен)
//  4) учет фактически добавленного и вывезенного груза на складе

//============================================
// ПОИСК СВОБОДНЫХ РЕСУРСОВ
//============================================

int findFreeBerthForShip(const Port *port, ShipType shipType)
{
    // проверяем указатель и тип до обращения к массиву совместимости
    if (port == NULL || shipType < 0 || shipType >= SHIP_TYPE_COUNT)
        return -1;

    for (int i = 0; i < port->berthCount; i++)
    {
        const Berth *b = &port->berths[i];

        // проверяем что причал свободен И совместим с типом судна
        if (b->occupiedByShip == -1 && b->compatibleShipFlags[shipType])
        {
            return i; // id ресурса = индекс в массиве
        }
    }
    return -1; // не нашли
}

int findFreeTugForShip(const Port *port, ShipType shipType)
{
    if (port == NULL || shipType < 0 || shipType >= SHIP_TYPE_COUNT)
        return -1;

    for (int i = 0; i < port->tugCount; i++)
    {
        const Tug *t = &port->tugs[i];

        if (t->busyWithShip == -1 && t->compatibleShipFlags[shipType])
        {
            return i;
        }
    }
    return -1;
}

int findFreeCraneForCargo(const Port *port, CargoType cargoType)
{
    if (port == NULL || cargoType < 0 || cargoType >= CARGO_TYPE_COUNT)
        return -1;

    for (int i = 0; i < port->craneCount; i++)
    {
        const Crane *c = &port->cranes[i];

        // кран выбирается по типу груза
        if (c->busyWithShip == -1 && c->compatibleCargoFlags[cargoType])
        {
            return i;
        }
    }
    return -1;
}

int findWarehouseForCargo(const Port *port, CargoType cargoType, double volume)
{
    if (port == NULL || cargoType < 0 || cargoType >= CARGO_TYPE_COUNT)
        return -1;
    if (!isfinite(volume) || volume <= 0.0)
        return -1;

    for (int i = 0; i < port->warehouseCount; i++)
    {
        const Warehouse *w = &port->warehouses[i];

        // склад должен быть совместим с грузом
        if (!w->compatibleCargoFlags[cargoType])
            continue;

        // загрузка должна находиться в пределах вместимости
        if (!isfinite(w->capacity) || !isfinite(w->currentLoad))
            continue;
        if (w->capacity < 0.0 || w->currentLoad < 0.0 || w->currentLoad > w->capacity)
            continue;

        double freeSpace = w->capacity - w->currentLoad;
        if (freeSpace >= volume)
            return i;
    }
    return -1;
}

//============================================
// ЗАНЯТИЕ РЕСУРСОВ
//============================================

int occupyBerth(Port *port, int berthId, int shipId)
{
    if (port == NULL || berthId < 0 || berthId >= port->berthCount || shipId < 0)
        return 0;

    Berth *b = &port->berths[berthId];

    // не занимаем причал повторно, даже тем же судном
    if (b->occupiedByShip != -1)
        return 0;

    b->occupiedByShip = shipId;
    return 1;
}

int occupyTug(Port *port, int tugId, int shipId, TugMission mission)
{
    if (port == NULL || tugId < 0 || tugId >= port->tugCount || shipId < 0)
        return 0;

    // занятому буксиру нужна миссия на вход или на выход
    if (mission != TUG_MISSION_ENTERING && mission != TUG_MISSION_LEAVING)
        return 0;

    Tug *t = &port->tugs[tugId];
    if (t->busyWithShip != -1)
        return 0;

    t->busyWithShip = shipId;
    t->mission = mission;
    return 1;
}

int occupyCrane(Port *port, int craneId, int shipId)
{
    if (port == NULL || craneId < 0 || craneId >= port->craneCount || shipId < 0)
        return 0;

    Crane *c = &port->cranes[craneId];
    if (c->busyWithShip != -1)
        return 0;

    c->busyWithShip = shipId;
    return 1;
}

double occupyWarehouse(Port *port, int warehouseId, double volume)
{
    if (port == NULL || warehouseId < 0 || warehouseId >= port->warehouseCount)
        return 0.0;
    if (!isfinite(volume) || volume <= 0.0)
        return 0.0;

    Warehouse *w = &port->warehouses[warehouseId];

    if (!isfinite(w->capacity) || !isfinite(w->currentLoad))
        return 0.0;
    if (w->capacity < 0.0 || w->currentLoad < 0.0 || w->currentLoad > w->capacity)
        return 0.0;

    double freeSpace = w->capacity - w->currentLoad;

    // если порция больше свободного места, выгружаем только часть
    if (volume >= freeSpace)
    {
        w->currentLoad = w->capacity;
        return freeSpace;
    }

    double previousLoad = w->currentLoad;
    w->currentLoad += volume;

    // возвращаем изменение загрузки с учетом округления double
    return w->currentLoad - previousLoad;
}

//============================================
// ОСВОБОЖДЕНИЕ РЕСУРСОВ
//============================================

void releaseBerth(Port *port, int berthId)
{
    if (port == NULL || berthId < 0 || berthId >= port->berthCount)
        return;
    port->berths[berthId].occupiedByShip = -1;
}

void releaseTug(Port *port, int tugId)
{
    if (port == NULL || tugId < 0 || tugId >= port->tugCount)
        return;

    Tug *t = &port->tugs[tugId];
    t->busyWithShip = -1;
    t->mission = TUG_FREE;
}

void releaseCrane(Port *port, int craneId)
{
    if (port == NULL || craneId < 0 || craneId >= port->craneCount)
        return;
    port->cranes[craneId].busyWithShip = -1;
}

double releaseWarehouse(Port *port, int warehouseId, double volume)
{
    if (port == NULL || warehouseId < 0 || warehouseId >= port->warehouseCount)
        return 0.0;
    if (!isfinite(volume) || volume <= 0.0)
        return 0.0;

    Warehouse *w = &port->warehouses[warehouseId];

    if (!isfinite(w->capacity) || !isfinite(w->currentLoad))
        return 0.0;
    if (w->capacity < 0.0 || w->currentLoad < 0.0 || w->currentLoad > w->capacity)
        return 0.0;

    // нельзя вывезти больше груза, чем сейчас лежит на складе
    if (volume >= w->currentLoad)
    {
        double removedVolume = w->currentLoad;
        w->currentLoad = 0.0;
        return removedVolume;
    }

    double previousLoad = w->currentLoad;
    w->currentLoad -= volume;
    return previousLoad - w->currentLoad;
}

//============================================
// КОМПЛЕКСНОЕ ОСВОБОЖДЕНИЕ
//============================================

void releaseAllShipResources(Port *port, Ship *ship)
{
    if (port == NULL || ship == NULL || ship->id < 0)
        return;

    // проверяем владельца по записям порта
    // старый id в судне не должен освободить чужой ресурс
    for (int i = 0; i < port->berthCount; i++)
    {
        if (port->berths[i].occupiedByShip == ship->id)
        {
            releaseBerth(port, i);
        }
    }

    for (int i = 0; i < port->tugCount; i++)
    {
        if (port->tugs[i].busyWithShip == ship->id)
        {
            releaseTug(port, i);
        }
    }

    for (int i = 0; i < port->craneCount; i++)
    {
        if (port->cranes[i].busyWithShip == ship->id)
        {
            releaseCrane(port, i);
        }
    }

    // сбрасываем записи ресурсов у судна
    ship->berthId = -1;

    for (int i = 0; i < MAX_TUGS_PER_SHIP; i++)
    {
        ship->tugIds[i] = -1;
    }
    ship->tugCount = 0;

    for (int i = 0; i < MAX_CRANES_PER_SHIP; i++)
    {
        ship->craneIds[i] = -1;
    }
    ship->craneCount = 0;

    // судно больше не связано со складом, но выгруженный груз остается
    // вывоз груза учитывается отдельно через releaseWarehouse
    ship->warehouseId = -1;
}
