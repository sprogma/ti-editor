#include "network_impl.h"
#include "requests.h"

#include <ds.h>
#include <mp.h>
#include <api.h>
#include <bestassert.h>
#include <random.h>
#include <scheduler.h>

#define ctx network_ctx

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

void net_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
    fflush(stdout);
}

void on_incoming_message(Channel *ch, const byte *data, u32 len) {
    Peer *peer = ch->peer;

    RequestHeader *header = (void *)data;

    char v[128];
    sprintf(v, "[%s:%hu/%llx]", peer->addr.direct.ip, peer->addr.direct.port, (u64)ch);

    switch (header->request_type) {
        case GET_OPERATION_BY_ID: {
            RequestGetOperationById *msg = (void *)data;
            if (has_or_remember_guid(msg->guid)) {
                return;
            }
            
            net_log("%s:GET_OPERATION_BY_ID id=%llu", v, msg->op_id);
            // check if operation exists locally
            Operation op;
            i32 res = get_operation_no_load(msg->op_id, &op);
            if (res == GET_OPERATION_SUCCESS) {
                const IOperation *iop = get_operation_type(op.type);
                assert(iop);
                
                u64 buffer_size = 1024;
                void *buffer = malloc(buffer_size);
                AnswerGetOperationById *ans = buffer;
                u64 used = iop->serialize(op.self, ans->data, buffer_size - sizeof(*ans));
                if (used > buffer_size - sizeof(*ans)) {
                    free(buffer);
                    buffer_size = used + sizeof(*ans);
                    buffer = malloc(buffer_size);
                    used = iop->serialize(op.self, ans->data, buffer_size - sizeof(*ans));
                    assert(used == buffer_size - sizeof(*ans));
                }
                ans->request_type = GET_OPERATION_BY_ID | ANSWER_MASK,
                random_bytes(&ans->guid, sizeof(ans->guid));
                ans->tid = op.type,
                ans->op_id = msg->op_id,
                ans->data_size = used;
                Channel *ans_ch = open_channel(peer, true);
                if (ans_ch == NULL) {
                    net_log("%s:GET_OPERATION_BY_ID id=%llu [channel creation failed]", v, msg->op_id);
                    free(buffer);
                    break;
                }
                if (channel_send(ans_ch, ans, sizeof(*ans) + ans->data_size, true) != 0) {
                    free(buffer);
                    net_log("%s:GET_OPERATION_BY_ID id=%llu [channel_send failed]", v, msg->op_id);
                    break;
                }
                net_log("%s:GET_OPERATION_BY_ID id=%llu [answered]: size=%lld", v, msg->op_id, sizeof(ans) + ans->data_size);
                free(buffer);
            } else {
                rwl_write(&ctx.operation_lock);
                i64 pos = hmgeti(ctx.operation_events, msg->op_id);
                if (pos == -1) {
                    hmput(ctx.operation_events, msg->op_id, NULL);
                    pos = hmlen(ctx.operation_events) - 1;
                }
                arrput(ctx.operation_events[pos].value, (OperationEvent){.target = peer->id});
                rwl_unwrite(&ctx.operation_lock);
                net_log("%s:GET_OPERATION_BY_ID id=%llu [rerequest it]", v, msg->op_id);
                send_request_operation(msg->op_id, msg->guid.value);
            }
        } break;
        case GET_OPERATION_BY_ID | ANSWER_MASK: {
            AnswerGetOperationById *msg = (void *)data;            
            if (has_or_remember_guid(msg->guid)) {
                return;
            }
            
            net_log("%s:GET_OPERATION_BY_ID:ANSWER type=%llu id=%llu size=%llu", v, msg->tid, msg->op_id, msg->data_size);
            OperationEvent *events;
            
            rwl_write(&ctx.operation_lock);
            i64 pos = hmgeti(ctx.operation_events, msg->op_id);
            if (pos != -1) {
                events = ctx.operation_events[pos].value;
                hmdel(ctx.operation_events, msg->op_id);
            }
            hmdel(ctx.operation_events, msg->op_id);
            rwl_unwrite(&ctx.operation_lock);
            if (pos == -1) {
                net_log("%s:GET_OPERATION_BY_ID:ANSWER -> no actions", v);
                return;
            }
            bool already_built = false;
            for (i64 i = 0; i < arrlen(events); ++i) {
                if (events[i].target == ctx.self_id && !already_built) {
                    net_log("%s:GET_OPERATION_BY_ID:ANSWER -> action #%lld = create operation", v, i);
                    build_operation(msg->tid, msg->op_id, msg->data, msg->data_size);
                    already_built = true;
                } else {
                    NetworkNode target = events[i].target;
                    Peer *next_peer = get_peer(target);
                    if (next_peer == NULL) {
                        net_log("%s:GET_OPERATION_BY_ID:ANSWER -> action #%lld = forward to %s:%hu [PEER OUTDATED]", v, i, next_peer->addr.direct.ip, next_peer->addr.direct.port);
                        continue;
                    }
                    Channel *next_ch = open_channel(next_peer, true);
                    if (next_ch == NULL) {
                        net_log("%s:GET_OPERATION_BY_ID:ANSWER -> action #%lld = forward to %s:%hu [CHANNEL CREATION FAILED]", v, i, next_peer->addr.direct.ip, next_peer->addr.direct.port);
                        continue;
                    }
                    net_log("%s:GET_OPERATION_BY_ID:ANSWER -> action #%lld = forwarding to %s:%hu", v, i, next_peer->addr.direct.ip, next_peer->addr.direct.port);
                    channel_send(next_ch, data, len, true);
                }
            }
            arrfree(events);
        } break;
        case SHARE_STATISTIC: {
            RequestShareStatistic *msg = (void *)data;
            peer->stat = msg->stat;
        } break;
        default:
            printf("UNHANDLED REQUEST TYPE\n");
            panic();
    }
}
