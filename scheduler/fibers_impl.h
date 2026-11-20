#ifndef FIBERS_IMPL_H
#define FIBERS_IMPL_H

#include "scheduler.h"

#include <bestassert.h>
#include <api.h>
#include <ds.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#else
#include <sys/mman.h>
#endif

typedef u64 (*TaskFunction)(void);

// TODO: cache it from GetSystemInfo...
#define STACK_PAGE_SIZE 4096

STRUCT (FiberInfo) {
    void *rsp;
    _Atomic u64 pending_cancellation;
};

typedef enum : u64 {
    WaitMode_None,
    WaitMode_All,
} WaitMode;

STRUCT (WorkerContext) {
    void *rsp;
    void (*loop)(void);
#ifdef _WIN32
    void *stack_base;
    void *stack_limit;
#endif
    WaitMode wait_mode;
    TaskId *wait_array;
    u64 wait_count;
    FiberInfo *current;
};

extern _Thread_local WorkerContext tls_worker;

// stack_mem = NULLPTR = allocate dynamic stack; if stack_mem == NULL, stack_size is not used
i32 fiber_init(FiberInfo *f, void *stack_mem, size_t stack_size, TaskFunction fn, void *arg);

// cancel_cose must be non zero
void fiber_cancel(FiberInfo *f, u64 cancel_code);

void internal_fiber_resume(void *target_rsp);
void internal_fiber_suspend(void **save_rsp, WorkerContext *sched);
void internal_fiber_exit(WorkerContext *sched) __attribute__((noreturn));
void internal_fiber_swap(void **save_rsp, void *target_rsp);
void internal_fiber_start(void);

#endif // FIBERS_IMPL_H
