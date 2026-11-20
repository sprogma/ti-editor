#include "fibers_impl.h"

#include <uv.h>
#include <mp.h>
#include <bestassert.h>

_Thread_local WorkerContext tls_worker;

#define GEN_MASK 0xFFFFFFFFull

STRUCT (Task) {
    FiberInfo *fiber;
    i64 priority;

    // telemetry
    _Atomic Time t_start_time;
    _Atomic Time t_last_event_time; // last 'start running' or 'start waiting' time
    _Atomic Time t_execution_self;
    _Atomic Time t_execution_wait;
};

STRUCT (TaskResult) {
    TaskId key;
    void *value;
};

static struct {

    rwlock results_lock;
    TaskResult *results;

    rwlock lock;
    
    Task *tasks;
    u32 *generations;
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

    // telemetry
    _Atomic Time t_idle;
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

static TaskId alloc_task(i64 priority) {
    rwl_write(&ctx.lock);

    i64 cell = ctx.free_head;
    if (cell == -1) {
        // TODO: realloc ctx.tasks up
        abort();
    }
    ctx.free_head = *(i64 *)&ctx.tasks[cell];
    i64 gen = ctx.generations[cell];

    rwl_unwrite(&ctx.lock);

    return (cell << 32) | gen;
}

static void free_task(TaskId id) {
    rwl_read(&ctx.lock);
    ctx.generations[id >> 32]++;
    rwl_unread(&ctx.lock);
}

static void enqueue_task(TaskId id) {
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


static FiberInfo *scheduler_next_fiber() {
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
        Time prev_event = atomic_exchange_explicit(&ctx.tasks[id].t_last_event_time, now, memory_order_relaxed);
        atomic_fetch_add_explicit(&ctx.tasks[id].t_execution_wait, now - prev_event, memory_order_relaxed);
    }

    rwl_unwrite(&ctx.lock);
    return fib;
}

static void worker_loop(void) {
    while (1) {
        // find next task
        FiberInfo *next = scheduler_next_fiber();
        if (next) {
            tls_worker.wait_mode = WaitMode_None;
            tls_worker.current = next;
            internal_fiber_resume(next->rsp);
        } else {
            Time t1 = uv_hrtime();
            uv_sleep(1);
            Time t2 = uv_hrtime();
            atomic_fetch_add_explicit(&ctx.t_idle, t2 - t1, memory_order_relaxed);
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

API_METHOD u64 wait(TaskId *ids, u64 count) {
    if (ids == NULL || count == 0) {
        FiberInfo *f = scheduler_next_fiber();
        if (f) {
            internal_fiber_swap(&tls_worker.current->rsp, f->rsp);
            return atomic_load_explicit(&tls_worker.current->pending_cancellation, memory_order_relaxed);
        } else {
            tls_worker.wait_mode = WaitMode_None;
            internal_fiber_suspend(&tls_worker.current->rsp, &tls_worker);
            return atomic_load_explicit(&tls_worker.current->pending_cancellation, memory_order_relaxed);
        }
    }
    rwl_read(&ctx.results_lock);
    for (u64 i = 0; i < count; ++i) {
        if (hmgeti(ctx.results, ids[i]) == -1) {
            rwl_unread(&ctx.results_lock);

            rwl_read(&ctx.lock);
            if (ctx.generations[ids[i] >> 32] != (ids[i] & GEN_MASK)) {
                rwl_read(&ctx.results_lock);
                continue;
            }
            rwl_unread(&ctx.lock);
            
            tls_worker.wait_mode = WaitMode_All;
            tls_worker.wait_array = ids + i;
            tls_worker.wait_count = count - i;
            internal_fiber_suspend(&tls_worker.current->rsp, &tls_worker);
            return atomic_load_explicit(&tls_worker.current->pending_cancellation, memory_order_relaxed);
        }
    }    
    rwl_unread(&ctx.results_lock);
    return atomic_load_explicit(&tls_worker.current->pending_cancellation, memory_order_relaxed);
}


static void *task_get_result(TaskId id, i32 *is_ready) {

    rwl_read(&ctx.lock);
    if (ctx.generations[id >> 32] != (id & GEN_MASK)) {
        if (is_ready) *is_ready = 0;
        return NULL;
    }
    rwl_unread(&ctx.lock);

    rwl_write(&ctx.results_lock);
    int pos = hmgeti(ctx.results, id);
    void *res = NULL;
    if (pos != -1) {
        if (is_ready) *is_ready = 0;
        res = ctx.results[pos].value;
        hmdel(ctx.results, id);
        free_task(id);
    } else {
        if (is_ready) *is_ready = 1;
    }
    rwl_unwrite(&ctx.results_lock);
    return res;
}

i64 task_get_anchor(TaskId id, i32 *is_ready) { return (i64)task_get_result(id, is_ready); }
Stat *task_get_stat(TaskId id, i32 *is_ready) { return task_get_result(id, is_ready); }
DataTaskResult *task_get_data(TaskId id, i32 *is_ready) { return task_get_result(id, is_ready); }

void task_unref_segment(DataTaskResult *result) {
    // TODO: group segments by type and use batching,
    //       and may be extract unref_segment(OperationId op, Segment *seg) to some function...
    Operation op;
    for (i64 i = 0; i < result->data_count; ++i) {
        if (result->data[i].op != InvalidOperation) {
            if (!get_operation(result->data[i].op, &op)) {
                abort();
            }
            const IOperation *iop = get_operation_type(op.type);
            iop->unref(op.self, &result->data[i].segment, 1);
        }
    }
}
