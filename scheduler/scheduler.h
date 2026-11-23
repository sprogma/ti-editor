#ifndef SCHEDULERS_H
#define SCHEDULERS_H

#include <api.h>

typedef void *(*TaskFunction)(void *);

void init_task_subsystem();

void worker_thread(void *arg);

#define is_local_task(id) (((id) & 0x8000000000000000ull) == 0)

TaskId start_local_task(TaskFunction fn, void *arg, i64 priority);
void stop_local_task(TaskId id, u64 code);

i64 local_task_get_anchor(TaskId id, i32 *is_ready);
Stat *local_task_get_stat(TaskId id, i32 *is_ready);
DataTaskResult *local_task_get_data(TaskId id, i32 *is_ready);

// !!! NOT UNREFS remote segments (InvalidOperation) -> so could be used to free only local results
void local_task_unref_segment(DataTaskResult *result);

#endif // SCHEDULERS_H
