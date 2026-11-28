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


