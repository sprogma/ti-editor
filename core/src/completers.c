#include <stdio.h>
#include <uv.h>

#include <ds.h>
#include <request.h>
#include <completers.h>
#include <types.h>


int next_client_id = 0;

ClientContext *complete_connection(uv_stream_t *client, char *name) {
    ClientContext *ctx = calloc(1, sizeof(ClientContext));
    ctx->client_id = next_client_id++;
    strncpy(ctx->client_name, name, sizeof(ctx->client_name) - 1);
    ctx->client_name[sizeof(ctx->client_name) - 1] = 0;
    printf("Get new connection from %s\n", ctx->client_name);
    ctx->type = RequestType_Incomplete;
    ctx->length = sizeof(RequestHeader);
    ctx->read = 0;
    return ctx;
}

void complete_request(uv_stream_t *client, ClientContext *ctx) {
    printf("Get request %lld of length %lld from %s\n", ctx->type, (long long)ctx->length, ctx->client_name);
}

void complete_cleanup(ClientContext *ctx) {
    // TODO: clean all structures
    (void)ctx;
}



