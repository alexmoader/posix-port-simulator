#include "output.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>


static FILE *logFile = NULL;


static void openLog(const char *filename, int append)
{
    closeLog();
    // получаем дескриптор через сис вызовы
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND | (append ? 0 : O_TRUNC), 0644);

    if (fd == -1) // не получилось открыть файл
    {
        return;
    }

    // получилось открыть файл
    logFile = fdopen(fd, "w");
    if (logFile == NULL)
    {
        close(fd); // Если fdopen не удался, закрываем fd вручную
        return;
    }
}

// Общая защита модели охватывает запись целого события.
void initLog(const char *filename)
{
    openLog(filename, 0);
}

void initWorkerLog(const char *filename)
{
    openLog(filename, 1);
}

void closeLog(void)
{
    if (logFile == NULL)
        return;
    fclose(logFile);
    logFile = NULL;
}

const char *shipTypeName(ShipType type)
{
    switch (type)
    {
    case SHIP_DRY_CARGO:
        return "сухогруз";

    case SHIP_TANKER:
        return "танкер";

    case SHIP_CONTAINER:
        return "контейнеровоз";

    case SHIP_REEFER:
        return "рефрижератор";

    default:
        return "неизвестный тип";
    }
}

const char *cargoTypeName(CargoType type)
{
    switch (type)
    {
    case CARGO_BULK:
        return "навалочный";

    case CARGO_LIQUID:
        return "наливной";

    case CARGO_CONTAINER:
        return "контейнер";

    case CARGO_REFRIGERATED:
        return "скоропорт";

    default:
        return "неизвестный тип";
    }
}

const char *shipStateName(ShipState state)
{
    switch (state)
    {
    case SHIP_ARRIVED:
        return "прибыло";

    case SHIP_WAITING_FOR_TUG_IN:
        return "ждёт буксир на вход";

    case SHIP_ENTERING:
        return "входит в порт";

    case SHIP_WAITING_FOR_BERTH:
        return "ждёт причал";

    case SHIP_MOORED:
        return "пришвартовано";

    case SHIP_WAITING_FOR_CRANE:
        return "ждёт кран";

    case SHIP_WAITING_FOR_WAREHOUSE:
        return "ждёт место на складе";

    case SHIP_LOADING:
        return "обрабатывается";

    case SHIP_WAITING_FOR_TUG_OUT:
        return "ждёт буксир на выход";

    case SHIP_LEAVING:
        return "выходит из порта";

    case SHIP_DEPARTED:
        return "покинуло порт";

    default:
        return "неизвестное состояние";
    }
}

void logEvent(const Port *port, const char *format, ...)
{
    va_list args; // создаем список аргументов

    // 1) печать в терминал (stdout)
    printf("[t=%6.2f] ", port->currentTime);

    va_start(args, format); // идем по аргументам после послденего известного
    vprintf(format, args);  // записываем в терминал по формату
    va_end(args);           // закрываем курсор

    printf("\n");

    // 2) печать в лог-файл (если открыт)
    if (logFile != NULL)
    {
        fprintf(logFile, "[t=%6.2f] ", port->currentTime);

        va_start(args, format);          // <- ВАЖНО: снова, потому что args уже потрачен
        vfprintf(logFile, format, args); // теперь печать идет в файл
        va_end(args);

        fprintf(logFile, "\n");

        fflush(logFile); // cбрасываем буфер, чтобы логи сразу писались на диск
    }
    fflush(stdout);
}

void printStatistics(const Port *port)
{
    printf("\n");
    printf("============================================\n");
    printf(" ИТОГОВАЯ СТАТИСТИКА\n");
    printf("============================================\n");
    printf("Модельное время:                         %.2f\n", port->currentTime);
    printf("Всего судов в модели:                    %d\n", port->shipCount);
    printf("Обработано (покинули порт):              %d\n", port->shipsDeparted);
    printf("Осталось в ожидании:                     %d\n", port->shipsWaiting);
    printf("Освобождений кранов при ожидании склада: %d\n", port->deadlocksPrevented);
    printf("============================================\n");

    printf("СОСТОЯНИЕ СУДОВ\n");
    for (int i = 0; i < port->shipCount; i++)
    {
        const Ship *s = &port->ships[i];
        printf("  Судно %d: %-28s груз обработан %.2f из %.2f, прибытие %.2f\n", s->id,
               shipStateName(s->state), s->cargoProcessed, s->cargo.volume, s->arrivalTime);
    }

    // продублировать в лог, если он открыт
    if (logFile != NULL)
    {
        fprintf(logFile, "\n============================================\n");
        fprintf(logFile, " ИТОГОВАЯ СТАТИСТИКА\n");
        fprintf(logFile, "============================================\n");
        fprintf(logFile, "Модельное время:                         %.2f\n", port->currentTime);
        fprintf(logFile, "Всего судов в модели:                    %d\n", port->shipCount);
        fprintf(logFile, "Обработано (покинули порт):              %d\n", port->shipsDeparted);
        fprintf(logFile, "Осталось в ожидании:                     %d\n", port->shipsWaiting);
        fprintf(logFile, "Освобождений кранов при ожидании склада: %d\n", port->deadlocksPrevented);
        fprintf(logFile, "============================================\n");
        fprintf(logFile, "СОСТОЯНИЕ СУДОВ\n");
        for (int i = 0; i < port->shipCount; i++)
        {
            const Ship *s = &port->ships[i];
            fprintf(logFile, "  Судно %d: %-28s груз обработан %.2f из %.2f, прибытие %.2f\n",
                    s->id, shipStateName(s->state), s->cargoProcessed, s->cargo.volume,
                    s->arrivalTime);
        }
        fflush(logFile);
    }
}
