#ifndef IPC_H
#define IPC_H

#include "runtime.h"

#define MEMORY_NAME "/acos42_memory"
#define MUTEX_NAME "/acos42_mutex"

// Общие объекты создает и удаляет только управляющая программа.
int createRuntime(PortRuntime *runtime, const Port *port, IpcMode mode);
int connectRuntime(PortRuntime *runtime);
void closeRuntime(PortRuntime *runtime);
void bindParticipants(PortRuntime *runtime);
int waitEvent(sem_t *event);
int lockModel(PortRuntime *runtime);
void unlockModel(PortRuntime *runtime);

#endif // IPC_H
