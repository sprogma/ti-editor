#include <stdio.h>
#include <uv.h>

#include <ds.h>
#include <request.h>
#include <completers.h>
#include <types.h>


int next_client_id = 0;

GlobalContext global_context = {
    .clients={
        .data={}, 
        .count=0,
        .lock=RWLOCK_INIT,
    },
    .chunks={
        .local=NULL,
        .lock=RWLOCK_INIT,
    }
};

ClientContext *complete_connection(uv_stream_t *client, char *name) {
    ClientContext *ctx = calloc(1, sizeof(ClientContext));
    ctx->client_id = next_client_id++;
    ctx->client = client;
    strncpy(ctx->client_name, name, sizeof(ctx->client_name) - 1);
    ctx->client_name[sizeof(ctx->client_name) - 1] = 0;
    printf("Get new connection from %s\n", ctx->client_name);
    ctx->type = RequestType_Incomplete;
    ctx->length = sizeof(RequestHeader);
    ctx->read = 0;
    ctx->global = &global_context;

    // add to main list
    rwl_write(&global_context.clients.lock);
    global_context.clients.data[global_context.clients.count++] = ctx;
    rwl_unwrite(&global_context.clients.lock);

    return ctx;
}

void complete_request(ClientContext *ctx) {

    printf("Get request %lld of length %lld from %s\n", ctx->type, (long long)ctx->length, ctx->client_name);

    switch (ctx->type) {
        case RequestType_PathChunk_GetError:
        case RequestType_PathChunk_Answer:
            complete_path_chunk(ctx);
            break;
    }
}

void complete_cleanup(ClientContext *ctx) {
    // TODO: clean all structures
    (void)ctx;
}

ClientContext *client_by_id(GlobalContext *gl, ClientId client) {
    ClientContext *res = NULL;
    rwl_read(&gl->clients.lock);
    for (int i = 0; i < gl->clients.count; ++i) {
        if (gl->clients.data[i]->client_id == client) {
            res = gl->clients.data[i];
            break;
        }
    }
    rwl_unread(&gl->clients.lock);
    return res;
}
