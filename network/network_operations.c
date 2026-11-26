#include "network_impl.h"

#include <ds.h>
#include <mp.h>
#include <api.h>
#include <bestassert.h>
#include <scheduler.h>

#define ctx network_ctx

/// operation ABF

static u64 bloom_hash(u64 id) {
    id = (id ^ (id >> 30)) * 0xbf58476d1ce4e5b9ULL;
    id = (id ^ (id >> 27)) * 0x94d049bb133111ebULL;
    return id ^ (id >> 31);
}

bool bloom_check(u64 *filter, u64 filter_size, u64 filter_arity, OperationId id) {
    u64 full_hash = bloom_hash(id);
    u32 low = (u32)full_hash;
    u32 high = full_hash >> 32;
    for (u32 k = 0; k < filter_arity; ++k) {
        u32 index = ((u64)(low + k * high) * filter_size) >> 32;
        u32 cell = index / 64, bit = index % 64;
        if ((filter[cell] & (1ull << bit)) == 0) {
            return false;
        }
    }
    return true;
}

void bloom_add(u64 *filter, u64 filter_size, u64 filter_arity, OperationId id) {
    u64 full_hash = bloom_hash(id);
    u32 low = (u32)full_hash;
    u32 high = full_hash >> 32;
    for (u32 k = 0; k < filter_arity; ++k) {
        u32 index = ((u64)(low + k * high) * filter_size) >> 32;
        u32 cell = index / 64, bit = index % 64;
        filter[cell] |= 1ull << bit;
    }
}

void request_operation(OperationId op_id) { 
    
    rwl_write(&ctx.operation_lock);
    i64 pos = hmgeti(ctx.operation_events, op_id);
    if (pos == -1) {
        hmput(ctx.operation_events, op_id, NULL);
        pos = hmlen(ctx.operation_events) - 1;
    }
    arrput(ctx.operation_events[pos].value, (OperationEvent){.target = ctx.self_id});
    rwl_unwrite(&ctx.operation_lock);
                
    send_request_operation(op_id);
}

void send_request_operation(OperationId op_id) { 
    // find peer which is responsible
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
        printf("request_operation failed: no peers\n");
        return;
    }
    
    // work with peers
    i32 *hops = NULL;
    arrsetlen(hops, count);
    i32 min_hops = max_hops;
    for (i64 i = 0; i < count; ++i) {
        hops[i] = max_hops;
        for (u64 k = 0; k < max_hops; ++k) {
            if (bloom_check(peers[i]->abf[k].filter, abf_size, abf_arity, op_id)) {
                hops[i] = k;
                break;
            }
        }
        if (hops[i] < min_hops) {
            min_hops = hops[i];
        }
    }

    i64 optimal = 0;
    for (i64 i = 0; i < count; ++i) {
        if (hops[i] == min_hops) {
            peers[optimal++] = peers[i];
        } else {
            unref_peer(peers[i]);
        }
    }
    arrsetlen(peers, optimal);
    arrfree(hops);

    i64 send_peers = 0;

    RequestGetOperationById req = {
        .request_type = GET_OPERATION_BY_ID,
        .op_id = op_id,
    };

    for (i64 i = 0; i < optimal; ++i) {
        if (peers[i]) {
            // TODO: send request via control stream
            Channel *control = get_control_channel(peers[i]);
            if (control == NULL) {
                continue;
            }
            if (channel_send(control, &req, sizeof(req), false) == 0) {
                send_peers++;
            }
        }
    }

    if (send_peers == 0) {
        // TODO: repeat request in another peers
        panic();
    }

    for (i64 i = 0; i < optimal; ++i) {
        unref_peer(peers[i]);
    }
    arrfree(peers);
}

void build_operation(OperationTypeId tid, OperationId op_id, void *data, u64 data_size) {
    deserialize_operation(tid, op_id, data, data_size);    
}

