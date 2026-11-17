#include "config.h"
#include "dispatcher.h"
#include "output.h"
#include "resources.h"
#include "process.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

// В данном модуле проверяется работа модели на небольших наборах.
//
// Внутри:
//  1) обработка двух судов через общий склад
//  2) остановка по времени
// Сигналы и независимые программы проверяются в test_processes.sh.

static void assertResourcesFree(const Port *port)
{
    for (int i = 0; i < port->berthCount; i++)
        assert(port->berths[i].occupiedByShip == -1);
    for (int i = 0; i < port->tugCount; i++)
    {
        assert(port->tugs[i].busyWithShip == -1);
        assert(port->tugs[i].mission == TUG_FREE);
    }
    for (int i = 0; i < port->craneCount; i++)
        assert(port->cranes[i].busyWithShip == -1);
    for (int i = 0; i < port->shipCount; i++)
    {
        const Ship *s = &port->ships[i];
        assert(s->berthId == -1 && s->warehouseId == -1);
        assert(s->tugCount == 0 && s->craneCount == 0);
    }
}

static void createTestPort(Port *port, double capacity)
{
    initPort(port);
    port->displayDelayUs = 0;
    port->timeLimit = 10000;
    port->warehouseUnloadRate = 0.5;

    const int shipFlags[] = {1, 1, 1, 1};
    const int cargoFlags[] = {1, 1, 1, 1};

    createBerth(port, shipFlags);
    createBerth(port, shipFlags);
    createTug(port, shipFlags);
    createTug(port, shipFlags);
    createCrane(port, cargoFlags, 4.0);
    createWarehouse(port, capacity, cargoFlags);
}

static void addTestShip(Port *port, double volume, double arrivalTime)
{
    Ship *s = &port->ships[port->shipCount];
    s->id = port->shipCount++;
    s->type = SHIP_TANKER;
    s->cargo.type = CARGO_LIQUID;
    s->cargo.volume = volume;
    s->arrivalTime = arrivalTime;
    s->state = SHIP_ARRIVED;
    s->stateStartTime = arrivalTime;
    s->berthId = -1;
    s->warehouseId = -1;
    for (int i = 0; i < MAX_TUGS_PER_SHIP; i++)
        s->tugIds[i] = -1;
    for (int i = 0; i < MAX_CRANES_PER_SHIP; i++)
        s->craneIds[i] = -1;
}

static void testSharedWarehouse(void)
{
    for (int strategy = DISPATCH_STAGED; strategy <= DISPATCH_RESERVED; strategy++)
    {
        Port p;
        createTestPort(&p, 2.0);
        p.dispatchStrategy = (DispatchStrategy)strategy;
        addTestShip(&p, 8, 0);
        addTestShip(&p, 6, 0);

        assert(runSimulation(&p) == 0);
        assert(p.shipsDeparted == 2 && p.shipsWaiting == 0);
        assert(p.ships[0].cargoProcessed == 8);
        assert(p.ships[1].cargoProcessed == 6);
        assertResourcesFree(&p);
    }
}

static void testTimeLimit(void)
{
    Port p;
    createTestPort(&p, 5.0);
    p.timeLimit = 0.3;
    p.timeStep = 0.2;
    addTestShip(&p, 10, 50);

    assert(runSimulation(&p) == 1);
    assert(p.currentTime == 0.3);
    assert(p.shipsDeparted == 0);
    assert(p.ships[0].cargoProcessed == 0.0);
    assertResourcesFree(&p);
}

int main(void)
{
    for (int mode = IPC_UNNAMED; mode <= IPC_NAMED; mode++)
    {
        simulationMode = (IpcMode)mode;
        testSharedWarehouse();
        testTimeLimit();
        closeLog();
    }
    puts("Dispatcher checks passed");
    return 0;
}
