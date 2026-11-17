#include "process.h"
#include "ipc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// В данном модуле находится отдельная программа участника порта.
// Запускается независимо от управляющей программы, как в примерах семинара 07.

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--list") == 0)
        return printWorkers();
    if (argc != 3)
    {
        fprintf(stderr, "Использование: port_worker ROLE ID\n");
        fprintf(stderr, "Роли: dispatcher, ship, berth, tug, crane, warehouse\n");
        return EXIT_FAILURE;
    }
    char *end;
    errno = 0;
    long id = strtol(argv[2], &end, 10);
    if (argv[2] == end || *end != '\0' || errno != 0 || id < 0 || id > INT_MAX)
    {
        fprintf(stderr, "Некорректный номер участника\n");
        return EXIT_FAILURE;
    }
    return runWorker(argv[1], (int)id);
}
