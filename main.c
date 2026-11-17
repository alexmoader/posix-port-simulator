#include "config.h"
#include "dispatcher.h"
#include "output.h"
#include "process.h"

#include <stdlib.h>

// В данном модуле находится точка входа приложения.
//
// Внутри:
//  1) настройка порта и разбор аргументов
//  2) создание ресурсов и судов
//  3) запуск симуляции и печать статистики

#ifndef DEFAULT_MODE
#define DEFAULT_MODE IPC_UNNAMED
#endif

int main(int argc, char **argv)
{
    Port port;
    initPort(&port);
    simulationMode = DEFAULT_MODE;

    int argsResult = parseArgs(argc, argv, &port);
    if (argsResult == 1)
        return EXIT_SUCCESS;
    if (argsResult == -1)
        return EXIT_FAILURE;

    createDefaultResources(&port);
    generateDemoShips(&port);
    printConfig(&port);

    int simulationResult = runSimulation(&port);

    printStatistics(&port);
    closeLog();

    return simulationResult == -1 ? EXIT_FAILURE : EXIT_SUCCESS;
}
