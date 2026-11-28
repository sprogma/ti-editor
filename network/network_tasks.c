#include "network_impl.h"
#include "requests.h"

#include <ds.h>
#include <mp.h>
#include <api.h>
#include <random.h>
#include <bestassert.h>
#include <scheduler.h>

#define ctx network_ctx

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

bool send_peer(Peer *peer, void *req, u64 req_size, bool use_control_stream) {
    if (use_control_stream) {
        Channel *control = get_control_channel(peer);
        if (control == NULL) {
            return false;
        }
        return channel_send(control, &req, req_size, false) == 0;
    } else {
        Channel *tmp_ch = open_channel(peer, true);
        if (tmp_ch == NULL) {
            return false;
        }
        return channel_send(tmp_ch, req, req_size, true) == 0;
    }
}

void send_to_node(NetworkNode node, void *req, u64 req_size, bool use_control_stream) {
    // find route to node
    Peer **peers = NULL;
    rwl_read(&ctx.peers_lock);
    i64 count = hmlen(ctx.peers);
    arrsetcap(peers, count);
    for (i64 i = 0; i < count; ++i) {
        Peer *p = ctx.peers[i].value;
        if (ref_peer(p)) {
            arrput(peers, p);
        }
    }
    rwl_unread(&ctx.peers_lock);

    count = arrlen(peers);
    if (count == 0) {
        printf("send_to_node failed: no peers\n");
        return;
    }

    i64 found = -1;
    for (i64 i = 0; i < count; ++i) {
        if (peers[i]->id == node) {
            found = i;
        }
    }

    if (found != -1) {
        send_peer(peers[found], req, req_size, use_control_stream);
    } else {
        i64 send_to = min(max(3, count / 3), count);
        i64 sent = 0;
        for (i64 i = 0; i < send_to; ++i) {
            sent += send_peer(peers[i], req, req_size, use_control_stream);
        }
        if (sent == 0) {
            // TODO: fallback
            printf("send_to_node failed: peers send failed\n");
            panic();
        }
    }

    for (i64 i = 0; i < count; ++i) {
        unref_peer(peers[i]);
    }
    arrfree(peers);
}

void network_local_run(TaskId *dest, const RequestInfo *info, i64 info_count) {
    // start local tasks
    for (u64 i = 0; i < info_count; ++i) {
        RequestInfo *copy = malloc(sizeof(*copy));
        *copy = info[i];
        dest[i] = start_local_task(local_request_completer, copy, 1);
    }
}

void network_send_run(NetworkNode node, TaskId *dest, const RequestInfo *info, i64 info_count, Guid *guid) {

    u64 total_arg_sizes = 0;
    for (i64 i = 0; i < info_count; ++i) {
        if (info[i].request_type == RequestInfoType_Stat) {
            total_arg_sizes += info[i].stat.args_size;
        }
    }
    NetworkRequestInfo *new_info;
    u64 total_info_sizes = sizeof(*send_info) * info_count;
    u64 req_size = sizeof(RequestStartTask) + total_info_sizes + total_arg_sizes;

    RequestStartTask *req = malloc(req_size);
    req->request_type = START_TASK;
    req->guid = *guid;
    req->node = node;
    req->requests_count = info_count;

    byte *send_body = (byte *)req + sizeof(*req);
    byte *data_base = send_body + total_info_sizes;
    new_info = send_data;
    for (i64 i = 0; i < info_count; ++i) {
        new_bytes(&new_info[i].request_guid, sizeof(new_info[i].request_guid));
        new_info[i].request_type = info[i].request_type;
        switch (info[i].request_type) {
            case RequestInfoType_Stat: {
                new_info[i].stat.type = info[i].stat.type;
                new_info[i].stat.args_size = info[i].stat.args_size;
                new_info[i].stat.args_offset = info[i].stat.args_size;
                memcpy(data_base, info[i].stat.args, info[i].stat.args_size);
                data_base += info[i].stat.args_size;
            } break;
            case RequestInfoType_Data: {
                new_info[i].data = info[i].data;
            } break;
            case RequestInfoType_Anchor: {
                new_info[i].anchor = info[i].anchor;
            } break;
        }
    }

    send_to_node(node, &req, req_size, true);

    TaskId *new_tasks = malloc(sizeof(*new_tasks) * info_count);
    for (u64 i = 0; i < info_count; ++i) {
        new_tasks[i] = register_remote_task();
        dest[i] = new_tasks[i];
    }

    rwl_write(&ctx.task_lock);
    struct GuidTaskKey v = {
        .guid = req->guid,
        .tasks = new_tasks,
        .tasks_len = info_count,
    };
    hmputs(ctx.task_events, data);
    rwl_unwrite(&ctx.task_lock);

    free(req);
}

void network_run(NetworkNode node, TaskId *dest, const RequestInfo *info, i64 info_count) {
    if (node == ctx.self_id) {
        network_local_run(dest, info, info_count);
    } else {
        Guid guid;
        random_bytes(&guid, sizeof(guid));
        network_send_run(node, dest, info, info_count, guid);
        has_or_remember_guid(guid);
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

static void remember_guid(const Guid *guid)
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
        remember_guid(&guid);
    }

    rwl_unwrite(&ctx.guid_lock);
    return res != -1;
}

