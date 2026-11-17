#ifndef PORT_TYPES_H
#define PORT_TYPES_H

// В данном модуле содержится фундамент приложения, а именно
// все структуры и типы данных для последующей работы.
// Здесь описаны:
//  - перечисления (типы судов, грузов и состояний);
//  - структуры участников модели (судно, причал, буксир, кран, склад);
//  - структура диспетчера (Port), агрегирующая все;
//  - константы максимальных размеров массивов.

//============================================
// КОНСТАНТЫ МАКСИМАЛЬНЫХ РАЗМЕРОВ
//============================================

#define MAX_BERTHS 16         // максимальное число причалов
#define MAX_TUGS 16           // максимальное число буксиров
#define MAX_CRANES 16         // максимальное число кранов
#define MAX_WAREHOUSES 8      // максимальное число складов
#define MAX_SHIPS 64          // максимальное число судов в модели
#define MAX_TUGS_PER_SHIP 4   // сколько буксиров может взять судно
#define MAX_CRANES_PER_SHIP 4 // сколько кранов может взять судно

//============================================
// ПЕРЕЧИСЛЕНИЯ
//============================================

// Тип судна, совместимость с причалами и буксирами
typedef enum
{
    SHIP_DRY_CARGO, // сухогруз
    SHIP_TANKER,    // танкер
    SHIP_CONTAINER, // контейнеровоз
    SHIP_REEFER,    // рефрижератор
    SHIP_TYPE_COUNT // счетчик типов - всегда последний
} ShipType;

// Тип груза, совместимость с кранами и складами
typedef enum
{
    CARGO_BULK,         // навалочный
    CARGO_LIQUID,       // наливной
    CARGO_CONTAINER,    // контейнеры
    CARGO_REFRIGERATED, // скоропорт
    CARGO_TYPE_COUNT
} CargoType;

// Текущая стадия жизненного цикла судна
typedef enum
{
    SHIP_ARRIVED,               // прибыло в порт, ждет диспетчера
    SHIP_WAITING_FOR_TUG_IN,    // ждет буксир на вход
    SHIP_ENTERING,              // входит (буксиры заняты)
    SHIP_WAITING_FOR_BERTH,     // ждет совместимый причал
    SHIP_MOORED,                // пришвартовано
    SHIP_WAITING_FOR_CRANE,     // ждет краны
    SHIP_WAITING_FOR_WAREHOUSE, // склад полон - условная приостановка
    SHIP_LOADING,               // грузится
    SHIP_WAITING_FOR_TUG_OUT,   // ждет буксир на выход
    SHIP_LEAVING,               // покидает порт (буксиры заняты)
    SHIP_DEPARTED,              // покинул порт (обработка завершена)
    SHIP_STATE_COUNT
} ShipState;

// Чем занят буксир (ведет судно на вход, на выход или свободен)
typedef enum
{
    TUG_FREE,             // свободен
    TUG_MISSION_ENTERING, // ведет судно на вход
    TUG_MISSION_LEAVING   // ведет судно на выход
} TugMission;

// Стратегия назначения ресурсов диспетчером
typedef enum
{
    DISPATCH_STAGED,  // ресурсы назначаются по отдельным этапам
    DISPATCH_RESERVED // нужный набор ресурсов резервируется вместе
} DispatchStrategy;

//============================================
// СТРУКТУРЫ-СУЩНОСТИ
//============================================

// Груз, перевозимый судном
typedef struct
{
    CargoType type; // тип груза
    double volume;  // объем груза в условных единицах
} Cargo;

// Причал
typedef struct
{
    int id;                                   // уникальный идентификатор причала
    int compatibleShipFlags[SHIP_TYPE_COUNT]; // совместимость с типами судов (1 - совместим)
    int occupiedByShip;                       // -1, если свободен, иначе - id судна
} Berth;

// Буксир
typedef struct
{
    int id;                                   // уникальный идентификатор буксира
    int compatibleShipFlags[SHIP_TYPE_COUNT]; // совместимость с типами судов (1 - совместим)
    int busyWithShip;                         // -1 если свободен, иначе - id судна
    TugMission mission;                       // осмысленно, только когда busyWithShip != -1
} Tug;

// Кран
typedef struct
{
    int id;                                     // уникальный идентификатор крана
    int compatibleCargoFlags[CARGO_TYPE_COUNT]; // совместимость с типами грузов (1 - совместим)
    int busyWithShip;                           // -1, если свободен, иначе - id судна
    double productivity;                        // грузооборот за единицу модельного времени
} Crane;

// Судно
typedef struct
{
    int id;                // уникальный номер
    ShipType type;         // тип судна
    Cargo cargo;           // груз
    double arrivalTime;    // время прибытия в модель
    ShipState state;       // текущая стадия
    double stateStartTime; // когда вошло в текущее состояние

    // Ресурсы, удерживаемые судном ПРЯМО СЕЙЧАС:
    int berthId; // -1, если причал не занят
    int tugIds[MAX_TUGS_PER_SHIP];
    int tugCount; // сколько буксиров уже занято
    int craneIds[MAX_CRANES_PER_SHIP];
    int craneCount;        // сколько кранов уже занято
    int warehouseId;       // -1, если склад не занят
    double cargoProcessed; // сколько груза уже обработано
} Ship;

// Склад
typedef struct
{
    int id;                                     // номер склада
    double capacity;                            // вместимость
    double currentLoad;                         // текущая загрузка
    int compatibleCargoFlags[CARGO_TYPE_COUNT]; // совместимость (1 - совместим)
} Warehouse;

//============================================
// ДИСПЕТЧЕР - агрегирует все ресурсы и суда
//============================================

typedef struct
{
    // Ресурсы

    // причалы
    Berth berths[MAX_BERTHS];
    int berthCount;

    // буксры
    Tug tugs[MAX_TUGS];
    int tugCount;

    // краны
    Crane cranes[MAX_CRANES];
    int craneCount;

    // склады
    Warehouse warehouses[MAX_WAREHOUSES];
    int warehouseCount;

    // суда
    Ship ships[MAX_SHIPS];
    int shipCount;

    // настройки модели
    double currentTime;                // текущее модельное время
    double timeStep;                   // шаг модельного времени
    double timeLimit;                  // предел моделирования (0 = нет)
    double enteringTime;               // сколько длится вход судна в порт
    double leavingTime;                // сколько длится выход судна из порта
    double warehouseUnloadRate;        // вывоз с каждого склада за единицу модельного времени
    DispatchStrategy dispatchStrategy; // стратегия назначения ресурсов
    int unlimitedMode;                 // 1 - режим без ограничения времени
    int displayDelayUs;                // задержка отображения (микросек)
    unsigned rngSeed;                  // seed ГСЧ для воспроизводимости

    // статистика
    double cargoRemoved; // сколько груза вывезено со складов
    int shipsDeparted;
    int shipsWaiting;
    int deadlocksPrevented; // сколько раз освободили кран при ожидании склада
} Port;

#endif // PORT_TYPES_H
