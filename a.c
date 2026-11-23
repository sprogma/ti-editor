#include <stdio.h>
#include <time.h>
#include <threads.h>

#define BESTASSERT_IMPLEMENTATION
#include <bestassert.h>

#include <api.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    // init subsystems
    CoreConfig *cfg = config_start();
    config_add_str(cfg, "module.path", "./test/native/native.dll");
    config_add_int(cfg, "net.port", port);
    config_add_int(cfg, "net.idle_timeout_ms", 60000);
    init_systems(cfg);

    OperationId opid = create_operation(0xAB0BA, NULL, 0);
    assert(opid != InvalidOperation);

    printf("opid = %lld", opid);

    Operation op;
    bool res2 = get_operation(opid, &op);
    assert(res2);
    const IOperation *iop = get_operation_type(op.type);
    assert(iop);
    
    RangeTask data[8];
    i64 res3 = iop->decompose(op.self, &(Range){{1, 1}, {2, -1}}, data, 8);
    assert(res3 > 0);

    for (int i = 0; i < res3; ++i) {
        if (data[i].op == InvalidOperation) {
            u64 siz = data[i].seg.size;
            byte *txt = data[i].seg.data;
            printf("res[%d] = <%.*s>\n", i, (int)siz, (char *)txt);
        } else {
            printf("res[%d] = [REF: op=%lld range=%lld %lld : %lld %lld]\n", 
                    i, data[i].ref.op, data[i].ref.range.l.anchor, data[i].ref.range.l.offset, data[i].ref.range.r.anchor, data[i].ref.range.r.offset);
        }
    }

    printf("raw api completed.\n");

    Sleep(60 * 1000);

//     RequestInfo rq[3] = {
//         {
//             .request_type = RequestInfoType_Data,
//             .data = {
//                 .op = opid,
//                 .range = {{1, 1}, {2, -1}},
//             },
//         },
//         {
//             .request_type = RequestInfoType_Anchor,
//             .anchor = {
//                 .base = 1,
//                 .request = 2,
//             },
//         },
//         {
//             .request_type = RequestInfoType_Data,
//             .data = {
//                 .op = opid,
//                 .range = {{1, 0}, {2, 0}},
//             },
//         },
//     };
// 
//     TaskId tsk[3];
// 
//     network_run(network_get_self(), tsk, rq, 3);
//     assert(tsk[0] != InvalidTaskId);
//     assert(tsk[1] != InvalidTaskId);
//     assert(tsk[2] != InvalidTaskId);
// 
//     struct timespec duration = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10 ms
// 
//     DataTaskResult *d0, *d2;
//     i64 a1;
//     int is_ready;
//     do { d0 = task_get_data(tsk[0], &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
//     do { a1 = task_get_anchor(tsk[1], &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
//     do { d2 = task_get_data(tsk[2], &is_ready); thrd_sleep(&duration, NULL); } while (!is_ready);
// 
//     for (int i = 0; i < d0->data_count; ++i) {
//         printf("Task0[%d] = <%.*s> from op=%lld\n", i, (int)d0->data[i].segment.size, (char *)d0->data[i].segment.data, d0->data[i].op);
//     }
//     printf("Task1 anchor offset is %lld\n", a1);
//     for (int i = 0; i < d2->data_count; ++i) {
//         printf("Task2[%d] = <%.*s> from op=%lld\n", i, (int)d0->data[i].segment.size, (char *)d0->data[i].segment.data, d0->data[i].op);
//     }
// 
//     task_unref_segment(d0);
//     task_unref_segment(d2);
    
    return 0;
}
