#include "network_impl.h"
#include "requests.h"

#include <ds.h>
#include <mp.h>
#include <api.h>
#include <bestassert.h>
#include <scheduler.h>

struct NetworkContext network_ctx;
#define ctx network_ctx

NetworkNode network_get_self() {
    return ctx.self_id;
}

u64 network_list(NetworkNode *nodes, u64 max_count) {
    rwl_read(&ctx.peers_lock);
    for (u64 i = 0; i < max_count && i < hmlen(ctx.peers); ++i) {
        nodes[i] = ctx.peers[i].key;
    }
    rwl_unread(&ctx.peers_lock);
    return hmlen(ctx.peers);
}

void network_stat(NetworkNode node, NetworkStat *stat) {
    if (node == ctx.self_id) {
        rwl_read(&ctx.stat_local_lock);
        stat->bandwidth = -1;
        stat->ping = -1;
        stat->idle_power = ctx.stat.idle_power;
        stat->free_ram = ctx.stat.free_ram;
        stat->free_disk = ctx.stat.free_disk;
        rwl_unread(&ctx.stat_local_lock);
    } else {
        rwl_read(&ctx.peers_lock);
        ptrdiff_t tmp;
        Peer *peer = hmget_ts(ctx.peers, node, tmp);
        rwl_unread(&ctx.peers_lock);
        
        fill_network_part(stat, peer);

        stat->idle_power = peer->stat.idle_power;
        stat->free_ram = peer->stat.free_ram;
        stat->free_disk = peer->stat.free_disk;
    }
}

static void get_data_req(OperationId op_id, Range *rg, ReferenceSegment **res_arr) {
    Operation op;
    i32 res = fiber_get_operation(op_id, &op);
    assert(res == GET_OPERATION_SUCCESS);
    const IOperation* iop = get_operation_type(op.type);
    assert(iop);

    RangeTask tmp[8];
    RangeTask *result = tmp;
    u64 count = iop->decompose(op.self, rg, tmp, sizeof(tmp)/sizeof(*tmp));
    if (count > 8) {
        RangeTask *big_tmp = malloc(sizeof(*big_tmp) * count);
        u64 new_count = iop->decompose(op.self, rg, tmp, count);
        assert(new_count == count);
        result = big_tmp;
    }

    for (u64 i = 0; i < count; ++i) {
        if (result[i].op == InvalidOperation) {
            ReferenceSegment item = {
                .op = op_id,
                .seg = result[i].seg,
            };
            arrput(*res_arr, item);
        } else {
            get_data_req(result[i].op, &result[i].ref.range, res_arr);
        }
    }
    
    if (result != tmp) {
        free(result);
    }
}

static void *local_request_completer(void *arg) {
    RequestInfo *rq = arg;
    switch (rq->request_type) {
        case RequestInfoType_Data: {
            OperationId op_id = rq->data.op;
            Range rng = rq->data.range;
            free(rq);
                    
            ReferenceSegment *res_arr = NULL;
            get_data_req(op_id, &rng, &res_arr);
            
            DataTaskResult *res = malloc(sizeof(*res));
            u64 total_size = sizeof(*res->data) * arrlen(res_arr);
            res->data = malloc(total_size);
            res->data_count = arrlen(res_arr);
            
            memcpy(res->data, res_arr, total_size);
            
            arrfree(res_arr);
            return res;
        } break;
        case RequestInfoType_Anchor: {
            OperationId op_id = rq->anchor.op;
            AnchorId base = rq->anchor.base;
            AnchorId request = rq->anchor.request;
            free(rq);
            
            Operation op;
            i32 res = fiber_get_operation(op_id, &op);
            assert(res == GET_OPERATION_SUCCESS);
            const IOperation* iop = get_operation_type(op.type);
            assert(iop);
            return (void *)iop->resolve_anchor(op.self, base, request);
        } break;
        case RequestInfoType_Stat: {
            const IStatistic *stat = get_statistic_type(rq->stat.type);
            assert(stat);
            
            return stat->calc(rq->stat.args, rq->stat.args_size);
        } break;
        default: {
            printf("Unknown request type\n");
            panic();
            // free(rq);
        } break;
    }
}

void network_run(NetworkNode node, TaskId *dest, const RequestInfo *info, u64 info_count) {
    if (node == ctx.self_id) {
        // start local tasks
        for (u64 i = 0; i < info_count; ++i) {
            RequestInfo *copy = malloc(sizeof(*copy));
            *copy = info[i];
            dest[i] = start_local_task(local_request_completer, copy, 1);
        }
    } else {
        printf("Network tasks are unsupported\n");
        panic();
    }
}

void network_stop(TaskId id, u64 cancel_code) {
    if (is_local_task(id)) {
        stop_local_task(id, cancel_code);
    } else {
        printf("Network tasks are unsupported\n");
        panic();
    }
}

// is ready returns 1 if task was really finished. owerwice res == NULL/0
// signed distance
i64 task_get_anchor(TaskId id, i32 *is_ready) {
    if (is_local_task(id)) {
        return local_task_get_anchor(id, is_ready);
    } else {
        printf("Network tasks are unsupported\n");
        panic();
    }
}
// result of calc
Stat *task_get_stat(TaskId id, i32 *is_ready) {
    if (is_local_task(id)) {
        return local_task_get_stat(id, is_ready);
    } else {
        printf("Network tasks are unsupported\n");
        panic();
    }
}
// get data. op_id can be InvalidOperation in some cases - but that is not error.
DataTaskResult *task_get_data(TaskId id, i32 *is_ready) {
    if (is_local_task(id)) {
        return local_task_get_data(id, is_ready);
    } else {
        printf("Network tasks are unsupported\n");
        panic();
    }
}
// safely frees remote and local segment from task_get_data
void task_unref_segment(DataTaskResult *result) {
    // free only local segments
    local_task_unref_segment(result);
    // free remote segments too
    for (u64 i = 0; i < result->data_count; ++i) {
        if (result->data[i].op == InvalidOperation) {
            // TODO: migrate to custom allocators for bytes
            // remote bytes -> free them
            free(result->data[i].seg.data);
        }
    }
}

bool has_or_remember_guid(Guid guid) {
    rwl_write(&ctx.guid_lock);
    Time now = get_time();
    
    i64 len = ctx.guid_buffer_size;
    if (len != 0) {
        i64 tail = ctx.guid_buffer_tail % len;
        while (ctx.guid_buffer_tail < ctx.guid_buffer_head && ctx.guid_buffer[tail].expires < now) {
            hmdel(ctx.guid_set, ctx.guid_buffer[tail].guid);
            tail = tail + 1 >= len ? 0 : tail + 1;
            ctx.guid_buffer_tail++;
        }
    }

    i64 res = hmgeti(ctx.guid_set, guid);
    if (res == -1) {
        hmputs(ctx.guid_set, (struct GuidKey){guid});
        
        // reallocate buffer
        if (ctx.guid_buffer_head - ctx.guid_buffer_tail >= len) {
            i64 new_len = len == 0 ? 1024 : len * 2;
            void *new_buf = malloc(new_len * sizeof(*ctx.guid_buffer));
            i64 tail_idx = len == 0 ? 0 : ctx.guid_buffer_tail % len;
            i64 first_part = len - tail_idx;
            memcpy(new_buf, ctx.guid_buffer + tail_idx, first_part * sizeof(*ctx.guid_buffer));
            if (tail_idx > 0) {
                memcpy((char *)new_buf + first_part * sizeof(*ctx.guid_buffer), ctx.guid_buffer, tail_idx * sizeof(*ctx.guid_buffer));
            }
            free(ctx.guid_buffer);
            ctx.guid_buffer = new_buf;
            ctx.guid_buffer_size = new_len;
            ctx.guid_buffer_tail = 0;
            ctx.guid_buffer_head = len;
        }
        assert(ctx.guid_buffer_size != 0);
        i64 head = ctx.guid_buffer_head % ctx.guid_buffer_size;
        ctx.guid_buffer[head].expires = now + ctx.guid_expiration_period;
        ctx.guid_buffer[head].guid = guid;
        ctx.guid_buffer_head++;
    }
    
    rwl_unwrite(&ctx.guid_lock);
    return res != -1;
}
