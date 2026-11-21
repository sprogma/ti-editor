#ifndef SCHEDULERS_H
#define SCHEDULERS_H

#include <api.h>

typedef void *(*TaskFunction)(void *);

void init_task_subsystem();

void worker_thread(void *arg);

TaskId start_local_task(TaskFunction fn, void *arg, i64 priority);
void stop_local_task(TaskId id, u64 code);

#endif // SCHEDULERS_H
