#include "stdio.h"
#include <bestassert.h>

#include <uv.h>
#include <api.h>
#include <operations.h>
#include <scheduler.h>

u64 f1(void) { uv_sleep(500); printf(__FUNCTION__ "!\n"); return 0; }
u64 f2(void) {
    uv_sleep(250);
    printf(__FUNCTION__ " calls f1...\n");
    TaskId tsk[64];
    for (int i = 0; i < 64; ++i) {
        tsk[i] = start_local_task(f1, (void *)100, 0);
    }
    printf(__FUNCTION__ " waits f1 x64...\n", tsk);
    wait(tsk, 64);
    
    printf(__FUNCTION__ " receives f1 x64...\n");
    for (int i = 0; i < 64; ++i) {
        i32 ready = 0;
        i64 res = task_get_anchor(tsk[i], &ready);
        printf(__FUNCTION__ "* from %lld -> %lld [ready = %d]...\n", tsk[i], res, ready);
    }
    return 0;
}

int main() {
    // init subsystems
    init_task_subsystem();
    init_operation_subsystem((0x1 << 32), 0);

    // fill operation & statistic types
    IOperation my_op = {
        // TODO: ...
    };
    // add_operation_type(1, &my_op);

    // populate thread pools
    for (int i = 0; i < 6; ++i) {
        uv_thread_t tid;
        assert(uv_thread_create(&tid, worker_thread, NULL) == 0);
    }

    // start local task
    printf("starting...\n");
    start_local_task(f1, (void *)100, 0);
    uv_sleep(1000);
    start_local_task(f1, (void *)100, 0);
    uv_sleep(1000);
    start_local_task(f1, (void *)100, 0);
    uv_sleep(1000);
    start_local_task(f1, (void *)100, 0);
    start_local_task(f1, (void *)100, 0);
    uv_sleep(1000);
    start_local_task(f1, (void *)100, 0);
    start_local_task(f1, (void *)100, 0);
    uv_sleep(1000);
    start_local_task(f2, (void *)100, 0);

    uv_sleep(200000);
    printf("2 second passed.\n");
    
    

    // OperationId res = create_operation(1, NULL, 0);
    // printf("OperationId = %d\n", res);
    // assert(res != InvalidOperation);

    return 0;
}
