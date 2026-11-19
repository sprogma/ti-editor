#include "fibers_impl.h"

#include <uv.h>
#include <mp.h>
#include <assert.h>

_Thread_local WorkerContext tls_worker;

STRUCT (Task) {
    FiberInfo *fiber;
    i64 priority;

    // telemetry
    Time t_start_time;
    Time t_last_event_time; // last 'start running' or 'start waiting' time
    Time t_execution_self;
    Time t_execution_wait;
};

struct {
    rwlock lock;

    Task *tasks;
    i32 *generations;
    i64 free_head;

    i64 tasks_count;
    i64 tasks_alloc;

    TaskId *queue;
    i64 queue_count;
    i64 queue_alloc;
    i64 queue_head;
    i64 queue_tail;

    TaskId *pending;
    i64 pending_count;
    i64 pending_alloc;
} ctx;


void init_task_subsystem() {
    rwl_init(&ctx.lock);
    rwl_write(&ctx.lock);

    { // tasks list
        ctx.tasks_alloc = 32 * 1024; // TODO: dynamic reallocation, less default
        ctx.tasks_count = 0;
        ctx.tasks = malloc(sizeof(*ctx.tasks) * ctx.tasks_alloc);

        i64 *writeback = &ctx.free_head;
        for (i32 i = 0; i < ctx.tasks_alloc; ++i) {
            *writeback = i;
            writeback = (i64 *)&ctx.tasks[i];
        }
        *writeback = -1;
    }

    { // queue
        ctx.queue_alloc = 32 * 1024;
        ctx.queue_head = 0;
        ctx.queue_tail = 0;
        ctx.queue = malloc(sizeof(*ctx.queue) * ctx.queue_alloc);
    }

    { // pending
        ctx.pending_alloc = 32 * 1024;
        ctx.pending_count = 0;
        ctx.pending = malloc(sizeof(*ctx.pending) * ctx.pending_alloc);
    }

    rwl_unwrite(&ctx.lock);
}

TaskId alloc_task(i64 priority) {
    rwl_write(&ctx.lock);

    i64 id = ctx.free_head;
    if (id == -1) {
        // TODO: realloc ctx.tasks up
        abort();
    }
    ctx.free_head = *(i64 *)&ctx.tasks[id];
    i64 gen = ctx.generations[id]++;

    rwl_unwrite(&ctx.lock);

    return (id << 32) | gen;
}

void enqueue_task(TaskId id) {
    rwl_write(&ctx.lock);

    if (ctx.queue_count == ctx.queue_alloc) {
        // TODO: realloc queue up
        abort();
    }
    ctx.queue_count++;
    i64 pos = ctx.queue_head;
    ctx.queue_head = (ctx.queue_head + 1) % ctx.queue_alloc;
    ctx.queue[pos] = id;

    rwl_unwrite(&ctx.lock);
}

TaskId start_local_task(TaskFunction fn, void *arg, i64 priority) {
    TaskId id = alloc_task(priority);

    rwl_read(&ctx.lock);

    Time now = uv_hrtime();

    Task *task = &ctx.tasks[id >> 32];
    *task = (Task) {
        .fiber = malloc(sizeof(*task->fiber)),
        .priority = priority,
        .t_start_time = now,
        .t_last_event_time = now,
        .t_execution_self = 0,
        .t_execution_wait = 0,
    };
    i32 res = fiber_init(task->fiber, NULL, 0, fn, arg);
    assert(res == 0);

    rwl_unread(&ctx.lock);

    return id;
}


FiberInfo *scheduler_next_fiber() {
    FiberInfo *fib = NULL;

    rwl_write(&ctx.lock);

    if (ctx.queue_count != 0) {
        i64 pos = ctx.queue_tail;
        ctx.queue_tail = (ctx.queue_tail + 1) % ctx.queue_alloc;
        ctx.queue_count--;

        TaskId id = ctx.queue[pos];
        ctx.queue[pos] = InvalidTaskId;

        fib = ctx.tasks[id].fiber;

        Time now = uv_hrtime();
        Time prev_event = ctx.tasks[id].t_last_event_time;
        ctx.tasks[id].t_execution_wait += now - prev_event;
        ctx.tasks[id].t_last_event_time = now;
    }

    rwl_unwrite(&ctx.lock);
    return fib;
}


void worker_loop(void) {
    while (1) {
        // find next task
        FiberInfo *next = scheduler_next_fiber();
        if (next) {
            tls_worker.current = next;
            internal_fiber_resume(next->rsp);
        } else {
            uv_sleep(1);
        }
    }
}

void task_worker() {
    u64 rsp = (u64)__builtin_frame_address(0);
    tls_worker.rsp = (void *)((rsp & ~0xFULL) - 8); // add space as for ret address
    tls_worker.loop = worker_loop;
#ifdef _WIN32
    tls_worker.stack_base  = (void *)__readgsqword(0x08);
    tls_worker.stack_limit = (void *)__readgsqword(0x10);
#endif
    tls_worker.current = NULL;
    worker_loop();
}

void schedule_task(TaskId task) {

    current;
}

API_METHOD u64 wait(TaskId *ids, u64 count) {
    if (ids == NULL || count == 0) {

        internal_fiber_suspend(&tls_worker->current->rsp, &tls_worker);

        OR

        FiberInfo *f = scheduler_next_fiber()
        interanl_fiber_swap(&tls_worker->current->rsp, f->rsp);
    }
    // TODO: something...
}


