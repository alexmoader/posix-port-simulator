#ifndef PROCESS_H
#define PROCESS_H

#include "runtime.h"

// Режим выбирается при сборке программы или перед запуском теста.
extern IpcMode simulationMode;
int printWorkers(void);
int runWorker(const char *role, int id);

#endif // PROCESS_H
