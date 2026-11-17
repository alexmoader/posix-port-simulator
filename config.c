#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

// В данном модуле содержатся настройки и инициализации модели.
//
// Внутри используется:
//  1) memset для обнуления порт
//  2) rand/srand для генерации судов
//  3) printf для печати конфигурации

//============================================
// СОЗДАНИЕ РЕСУРСОВ
//============================================

void createBerth(Port *port, const int *compatibleShipFlags)
{
    // Проверка на переполнение причалов
    if (port->berthCount >= MAX_BERTHS)
    {
        fprintf(stderr, "createBerth: превышено максимальное количество причалов (%d)\n",
                MAX_BERTHS);
        return;
    }

    Berth *b = &port->berths[port->berthCount];
    b->id = port->berthCount;

    for (int i = 0; i < SHIP_TYPE_COUNT; i++)
    {
        b->compatibleShipFlags[i] = compatibleShipFlags[i];
    }

    b->occupiedByShip = -1; // причал свободен
    port->berthCount++;     // увеличиваем колво причалов
}

void createTug(Port *port, const int *compatibleShipFlags)
{
    if (port->tugCount >= MAX_TUGS)
    {
        fprintf(stderr, "createTug: превышено максимальное количество буксировов (%d)\n", MAX_TUGS);
        return;
    }

    Tug *t = &port->tugs[port->tugCount];
    t->id = port->tugCount;

    for (int i = 0; i < SHIP_TYPE_COUNT; i++)
    {
        t->compatibleShipFlags[i] = compatibleShipFlags[i];
    }

    t->busyWithShip = -1;
    t->mission = TUG_FREE;
    port->tugCount++;
}

void createCrane(Port *port, const int *compatibleCargoFlags, double productivity)
{
    if (port->craneCount >= MAX_CRANES)
    {
        fprintf(stderr, "createCrane: превышен лимит кранов (%d)\n", MAX_CRANES);
        return;
    }

    Crane *c = &port->cranes[port->craneCount];
    c->id = port->craneCount;

    for (int i = 0; i < CARGO_TYPE_COUNT; i++)
    {
        c->compatibleCargoFlags[i] = compatibleCargoFlags[i];
    }

    c->busyWithShip = -1;
    c->productivity = productivity;
    port->craneCount++;
}

void createWarehouse(Port *port, double capacity, const int *compatibleCargoFlags)
{
    if (port->warehouseCount >= MAX_WAREHOUSES)
    {
        fprintf(stderr, "createWarehouse: превышен лимит складов (%d)\n", MAX_WAREHOUSES);
        return;
    }

    Warehouse *w = &port->warehouses[port->warehouseCount];
    w->id = port->warehouseCount;

    for (int i = 0; i < CARGO_TYPE_COUNT; i++)
    {
        w->compatibleCargoFlags[i] = compatibleCargoFlags[i];
    }

    w->capacity = capacity;
    w->currentLoad = 0.0;
    port->warehouseCount++;
}

//============================================
// ИНИЦИАЛИЗАЦИЯ ПОРТА
//============================================

void initPort(Port *port)
{
    // 1) обнулить все
    memset(port, 0, sizeof(Port));

    // 2) настроить модель по умолчанию
    port->currentTime = 0.0;
    port->timeStep = 1.0;    // один такт = 1 условная минута
    port->timeLimit = 100.0; // моделируем 100 тактов
    port->enteringTime = 2.0;
    port->leavingTime = 3.0;
    port->warehouseUnloadRate = 2.0; // вывоз с каждого склада за условную минуту
    port->dispatchStrategy = DISPATCH_STAGED;

    port->unlimitedMode = 0;
    port->displayDelayUs = 100000; // 0.1 сек между тактами
    port->rngSeed = (unsigned)time(NULL);
}

//============================================
// СОЗДАНИЕ НАБОРОВ РЕСУРСОВ ПО УМОЛЧАНИЮ
//============================================

void createDefaultBerths(Port *port)
{
    // создаем сометимости для причалов
    //  ind_0 - DRY, ind_1 - TANKER, ind_2 - CONT, ind_3 - REEFER
    const int berth0[] = {1, 0, 1, 0};
    const int berth1[] = {0, 1, 0, 0};
    const int berth2[] = {1, 0, 1, 0};
    const int berth3[] = {0, 0, 1, 1};

    // создаем сами причалы
    createBerth(port, berth0);
    createBerth(port, berth1);
    createBerth(port, berth2);
    createBerth(port, berth3);
}

void createDefaultTugs(Port *port)
{
    const int tugAll[] = {1, 1, 1, 1}; // всех принимает

    createTug(port, tugAll);
    createTug(port, tugAll);
}

void createDefaultCranes(Port *port)
{
    const int craneBulk[] = {1, 0, 0, 0};
    const int craneLiquid[] = {0, 1, 0, 0};
    const int craneCont[] = {0, 0, 1, 0};
    const int craneReef[] = {0, 0, 0, 1};
    const int craneBulk2[] = {1, 0, 0, 0};

    createCrane(port, craneBulk, 5.0);
    createCrane(port, craneLiquid, 4.0);
    createCrane(port, craneCont, 6.0);
    createCrane(port, craneReef, 3.0);
    createCrane(port, craneBulk2, 4.0);
}

void createDefaultWarehouses(Port *port)
{
    const int whBulk[] = {1, 0, 0, 0};
    const int whCont[] = {0, 0, 1, 0};
    const int whUniv[] = {0, 1, 0, 1}; // liquid + reefer

    createWarehouse(port, 1000.0, whBulk);
    createWarehouse(port, 500.0, whCont);
    createWarehouse(port, 300.0, whUniv);
}

void createDefaultResources(Port *port)
{
    createDefaultBerths(port);
    createDefaultTugs(port);
    createDefaultCranes(port);
    createDefaultWarehouses(port);
}

//============================================
// ГЕНЕРАЦИЯ СУДОВ
//============================================

void generateDemoShips(Port *port)
{
    srand(port->rngSeed); // позволяет выбрать сид для генерации случайный чисел + отладка

    const int shipCount = 8;

    for (int i = 0; i < shipCount; i++)
    {
        if (port->shipCount >= MAX_SHIPS)
            break;

        Ship *s = &port->ships[port->shipCount];
        s->id = port->shipCount;

        // случайный тип судна
        s->type = (ShipType)(rand() % SHIP_TYPE_COUNT);

        // груз соответствующий типу судна
        switch (s->type)
        {
        case SHIP_DRY_CARGO:
            s->cargo.type = CARGO_BULK;
            break;

        case SHIP_TANKER:
            s->cargo.type = CARGO_LIQUID;
            break;

        case SHIP_CONTAINER:
            s->cargo.type = CARGO_CONTAINER;
            break;

        case SHIP_REEFER:
            s->cargo.type = CARGO_REFRIGERATED;
            break;

        default:
            s->cargo.type = CARGO_BULK;
            break;
        }
        // случайный объем груза 100 ... 500
        s->cargo.volume = 100.0 + (rand() % 401);
        // в режиме без лимита используем окно прибытия 50 минут
        // ограничиваем значение до преобразования double в int
        double arrivalSpan = port->timeLimit / 2.0;
        if (port->unlimitedMode || port->timeLimit == 0.0)
            arrivalSpan = 50.0;
        if (arrivalSpan > INT_MAX)
            arrivalSpan = INT_MAX;
        int span = (int)arrivalSpan;
        if (span <= 0)
            span = 1;
        s->arrivalTime = (double)(rand() % span);

        // начальное состояние - прибыло в порт
        s->state = SHIP_ARRIVED;
        s->stateStartTime = s->arrivalTime;

        // ресурсы пока не удерживаются
        s->berthId = -1;
        s->tugCount = 0;
        s->craneCount = 0;
        s->warehouseId = -1;
        s->cargoProcessed = 0.0;

        for (int j = 0; j < MAX_TUGS_PER_SHIP; j++)
            s->tugIds[j] = -1;
        for (int j = 0; j < MAX_CRANES_PER_SHIP; j++)
            s->craneIds[j] = -1;

        port->shipCount++;
    }
}

//============================================
// РАЗБОР АРГУМЕНТОВ КОМАНДНОЙ СТРОКИ
//============================================

static int parseDoubleValue(const char *text, double *value)
{
    char *end;
    errno = 0;
    double parsed = strtod(text, &end);

    // строка должна целиком содержать конечное число
    if (text == end || *end != '\0' || errno != 0 || !isfinite(parsed))
        return 0;

    *value = parsed;
    return 1;
}

static int parseUnsignedValue(const char *text, unsigned *value)
{
    char *end;
    if (*text < '0' || *text > '9')
        return 0;

    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (*end != '\0' || errno != 0 || parsed > UINT_MAX)
        return 0;

    *value = (unsigned)parsed;
    return 1;
}

int parseArgs(int argc, char **argv, Port *port)
{
    for (int i = 1; i < argc; i++)
    {
        // help
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            printf("Использование: port_sim [опции]\n");
            printf("  --help              показать эту справку\n");
            printf("  --seed=N            seed генератора случайных чисел\n");
            printf("  --time-limit=N      предел модельного времени (0 = нет)\n");
            printf("  --step=N            шаг модельного времени (> 0)\n");
            printf("  --enter-time=N      длительность входа судна (> 0)\n");
            printf("  --leave-time=N      длительность выхода судна (> 0)\n");
            printf("  --strategy=NAME     стратегия: staged или reserved\n");
            printf("  --unlimited         работать до ухода всех судов или Ctrl+C\n");
            printf("  --delay-us=N        задержка отображения в микросекундах\n");
            printf("  --warehouse-rate=N  вывоз с каждого склада за единицу времени (> 0)\n");
            return 1;
        }

        if (strcmp(argv[i], "--unlimited") == 0)
        {
            port->unlimitedMode = 1;
            continue;
        }

        if (strncmp(argv[i], "--strategy=", 11) == 0)
        {
            const char *strategy = argv[i] + 11;
            if (strcmp(strategy, "staged") == 0)
            {
                port->dispatchStrategy = DISPATCH_STAGED;
            }
            else if (strcmp(strategy, "reserved") == 0)
            {
                port->dispatchStrategy = DISPATCH_RESERVED;
            }
            else
            {
                fprintf(stderr, "parseArgs: --strategy должна быть staged или reserved\n");
                return -1;
            }
            continue;
        }

        if (strncmp(argv[i], "--seed=", 7) == 0)
        {
            if (!parseUnsignedValue(argv[i] + 7, &port->rngSeed))
            {
                fprintf(stderr, "parseArgs: некорректный seed\n");
                return -1;
            }
            continue;
        }

        if (strncmp(argv[i], "--delay-us=", 11) == 0)
        {
            unsigned delay;
            if (!parseUnsignedValue(argv[i] + 11, &delay) || delay > INT_MAX)
            {
                fprintf(stderr, "parseArgs: задержка должна быть целым числом от 0 до %d\n",
                        INT_MAX);
                return -1;
            }
            port->displayDelayUs = (int)delay;
            continue;
        }

        if (strncmp(argv[i], "--time-limit=", 13) == 0)
        {
            double value;
            if (!parseDoubleValue(argv[i] + 13, &value) || value < 0.0)
            {
                fprintf(stderr, "parseArgs: --time-limit должен быть >= 0\n");
                return -1;
            }
            port->timeLimit = value;
            continue;
        }

        if (strncmp(argv[i], "--step=", 7) == 0)
        {
            double value;
            if (!parseDoubleValue(argv[i] + 7, &value) || value <= 0.0)
            {
                fprintf(stderr, "parseArgs: --step должен быть > 0\n");
                return -1;
            }
            port->timeStep = value;
            continue;
        }

        if (strncmp(argv[i], "--enter-time=", 13) == 0)
        {
            double value;
            if (!parseDoubleValue(argv[i] + 13, &value) || value <= 0.0)
            {
                fprintf(stderr, "parseArgs: --enter-time должен быть > 0\n");
                return -1;
            }
            port->enteringTime = value;
            continue;
        }

        if (strncmp(argv[i], "--leave-time=", 13) == 0)
        {
            double value;
            if (!parseDoubleValue(argv[i] + 13, &value) || value <= 0.0)
            {
                fprintf(stderr, "parseArgs: --leave-time должен быть > 0\n");
                return -1;
            }
            port->leavingTime = value;
            continue;
        }

        if (strncmp(argv[i], "--warehouse-rate=", 17) == 0)
        {
            double value;
            if (!parseDoubleValue(argv[i] + 17, &value) || value <= 0.0)
            {
                fprintf(stderr, "parseArgs: --warehouse-rate должен быть > 0\n");
                return -1;
            }
            port->warehouseUnloadRate = value;
            continue;
        }

        fprintf(stderr, "parseArgs: неизвестный аргумент '%s'\n", argv[i]);
        return -1;
    }
    return 0;
}

//============================================
// ПЕЧАТЬ КОНФИГУРАЦИИ
//============================================

void printConfig(const Port *port)
{
    printf("============================================\n");
    printf(" КОНФИГУРАЦИЯ МОДЕЛИ\n");
    printf("============================================\n");

    printf("Шаг времени:    %.2f\n", port->timeStep);
    printf("Лимит времени:  %.2f\n", port->timeLimit);
    printf("Время входа:    %.2f\n", port->enteringTime);
    printf("Время выхода:   %.2f\n", port->leavingTime);
    printf("Seed ГСЧ:       %u\n", port->rngSeed);
    printf("Задержка:       %d мкс\n", port->displayDelayUs);
    printf("Режим без лимита: %s\n", port->unlimitedMode || port->timeLimit == 0.0 ? "да" : "нет");
    printf("Стратегия:       %s\n",
           port->dispatchStrategy == DISPATCH_STAGED ? "поэтапная" : "резервирование набора");
    printf("Вывоз со склада: %.2f за единицу времени\n", port->warehouseUnloadRate);

    printf("\n--- Причалы (%d) ---\n", port->berthCount);
    for (int i = 0; i < port->berthCount; i++)
    {
        const Berth *b = &port->berths[i];
        printf("  Причал %d: совместим с [", b->id);
        for (int t = 0; t < SHIP_TYPE_COUNT; t++)
        {
            if (b->compatibleShipFlags[t])
                printf("%d ", t);
        }
        printf("]\n");
    }

    printf("\n--- Буксиры (%d) ---\n", port->tugCount);
    for (int i = 0; i < port->tugCount; i++)
    {
        printf("  Буксир %d\n", port->tugs[i].id);
    }

    printf("\n--- Краны (%d) ---\n", port->craneCount);
    for (int i = 0; i < port->craneCount; i++)
    {
        const Crane *c = &port->cranes[i];
        printf("  Кран %d: производительность %.2f, совместим с [", c->id, c->productivity);
        for (int t = 0; t < CARGO_TYPE_COUNT; t++)
        {
            if (c->compatibleCargoFlags[t])
                printf("%d ", t);
        }
        printf("]\n");
    }

    printf("\n--- Склады (%d) ---\n", port->warehouseCount);
    for (int i = 0; i < port->warehouseCount; i++)
    {
        const Warehouse *w = &port->warehouses[i];
        printf("  Склад %d: вместимость %.1f, совместим с [", w->id, w->capacity);
        for (int t = 0; t < CARGO_TYPE_COUNT; t++)
        {
            if (w->compatibleCargoFlags[t])
                printf("%d ", t);
        }
        printf("]\n");
    }

    printf("\n--- Суда (%d) ---\n", port->shipCount);
    for (int i = 0; i < port->shipCount; i++)
    {
        const Ship *s = &port->ships[i];
        printf("  Судно %d: тип=%d, груз(тип=%d, объем=%.1f), прибытие=%.1f\n", s->id, s->type,
               s->cargo.type, s->cargo.volume, s->arrivalTime);
    }
    printf("============================================\n\n");
}
