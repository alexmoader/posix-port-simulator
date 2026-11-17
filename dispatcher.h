#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "port_types.h"

// Проверяет параметры модели и наличие совместимых ресурсов.
int validatePort(const Port *port);
// Выполняет симуляцию до завершения судов или остановки модели.
int runSimulation(Port *port);

#endif // DISPATCHER_H
