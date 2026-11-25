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

#define max_hops 5
#define abf_size 1024
#define abf_arity 3

void download_operation(OperationId op_id) { 
    // find peer which is responsible
    Peer **peers = NULL;
    rwl_read(&ctx.peers_lock);
    i64 count = hmlen(peers);
    arrsetcap(peers, count);
    for (i64 i = 0; i < count; ++i) {
        Peer *p = ctx.peers[i].value;
        if (ref_peer(p)) {
            arrput(peers, p);
        }
    }
    rwl_unread(&ctx.peers_lock);

    count = arrlen(peers);
    
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

    for (i64 i = 0; i < optimal; ++i) {
        if (peers[i]) {
            // TODO: send request via control stream
        }
    }

    for (i64 i = 0; i < optimal; ++i) {
        unref_peer(peers[i]);
    }
    arrfree(peers);
}

