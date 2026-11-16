#include <stdio.h>
#include <assert.h>
#include <uv.h>

#include <ds.h>
#include <mp.h>
#include <request.h>
#include <completers.h>
#include <types.h>
#include <data.h>

#define PATH_TTL T_SEC(600)

typedef struct {
    ClientId client;
    i32 distance;
} Path;

typedef struct {
    ChunkId chunk;
    i32 bad_source_distance; // distance which should be broken
} Req_GetError;

typedef struct {
    ChunkId chunk;
    i32 distance;
} Req_Answer;

Path get_path(GlobalContext *gl, ChunkId chunk) {
    rwl_read(&gl->path_chunk.lock);
    i64 idx = hmgeti(gl->path_chunk.data, chunk);
    Path res = {-1, -1};
    if (idx != -1 && get_time() < gl->path_chunk.data[idx].expires) {
        res.client = gl->path_chunk.data[idx].client;
        res.distance = gl->path_chunk.data[idx].distance;
    }
    rwl_unread(&gl->path_chunk.lock);
    return res;
}

bool update_path(GlobalContext *gl, ClientId client, ChunkId chunk, i32 distance) {
    bool res = false;
    rwl_write(&gl->path_chunk.lock);
    i64 idx = hmgeti(gl->path_chunk.data, chunk);
    if (idx == -1 || get_time() > gl->path_chunk.data[idx].expires || distance < gl->path_chunk.data[idx].distance) {
        PathChunkKeyValue val = {
            .key = chunk,
            .client = client,
            .distance = distance,
            .expires = timeout_from_now(PATH_TTL),
        };
        hmputs(gl->path_chunk.data, val);
        res = true;
    }
    rwl_unwrite(&gl->path_chunk.lock);
    return res;
}

void clear_path(GlobalContext *gl, ChunkId chunk) {
    rwl_write(&gl->path_chunk.lock);
    hmdel(gl->path_chunk.data, chunk);
    rwl_unwrite(&gl->path_chunk.lock);
}

void handle_geterror(ClientContext *ctx) {
    GlobalContext *gl = ctx->global;
    Req_GetError *data = (void *)ctx->buffer;
    
    // if we know path to chunk -> answer it to sender
    Path now = get_path(gl, data->chunk);

    if (now.client == ctx->client_id) {
        // ignore strange request, possibly race condition
        return;
    }

    if (now.distance != -1 && data->bad_source_distance > now.distance) {
        Req_Answer ans = {
            .chunk = data->chunk,
            .distance = now.distance + 1,
        };
        send_body(ctx->client, RequestType_PathChunk_Answer, &ans, sizeof(ans));
    } else {
        clear_path(gl, data->chunk);
        
        Req_GetError ans = {
            .chunk = data->chunk,
            .bad_source_distance = data->bad_source_distance + 1,
        };
        
        // broadcast request to all neighbours
        rwl_read(&gl->clients.lock);
        for (i32 i = 0; i < gl->clients.count; ++i) {
            if (gl->clients.data[i]->client_id != ctx->client_id) {
                send_body(gl->clients.data[i]->client, RequestType_PathChunk_GetError, &ans, sizeof(ans));
            }
        }
        rwl_unread(&gl->clients.lock);
    }
}

void handle_answer(ClientContext *ctx) {
    GlobalContext *gl = ctx->global;
    Req_Answer *data = (void *)ctx->buffer;
    
    bool changed = update_path(gl, ctx->client_id, data->chunk, data->distance);
    if (changed) {
        // send all pending requests to this peer
        rwl_write(&gl->path_chunk.pending_lock);
        int idx = hmgeti(gl->path_chunk.pending, data->chunk);
        PendingRequest *requests = gl->path_chunk.pending[idx].pending;
        hmdel(gl->path_chunk.pending, data->chunk);
        rwl_unwrite(&gl->path_chunk.pending_lock);
        if (requests) {
            for (int i = 0; i < arrlen(requests); ++i) {
                send_packet(ctx->client, requests[i].packet, requests[i].size);
            }
            arrfree(requests);
        }

        // broadcast better results to neighbours
        Req_Answer ans = {
            .chunk = data->chunk,
            .distance = data->distance + 1,
        };
        // broadcast request to all neighbours
        rwl_read(&gl->clients.lock);
        for (i32 i = 0; i < gl->clients.count; ++i) {
            if (gl->clients.data[i]->client_id != ctx->client_id) {
                send_body(gl->clients.data[i]->client, RequestType_PathChunk_Answer, &ans, sizeof(ans));
            }
        }
        rwl_unread(&gl->clients.lock);
    }
}

void complete_path_chunk(ClientContext *ctx) {
    switch (ctx->type) {
        case RequestType_PathChunk_GetError: 
            handle_geterror(ctx);
            break;
        case RequestType_PathChunk_Answer: 
            handle_answer(ctx);
            break;
        default:
            assert(false);
    }
}

int send_chunk_packet(GlobalContext *gl, ChunkId chunk, const void *packet, i64 len) {
    rwl_write(&gl->path_chunk.pending_lock);
    Path path = get_path(gl, chunk);
    if (path.distance == -1) {
        int idx = hmgeti(gl->path_chunk.pending, chunk);
        PendingRequest req = {.packet = malloc(len), .size = len};
        memcpy(req.packet, packet, len);
        if (idx == -1) {
            PendingUnicastKeyValue value = {
                .key = chunk,
                .pending = NULL,
            };
            arrput(value.pending, req);
            hmputs(gl->path_chunk.pending, value);
        } else {
            arrput(gl->path_chunk.pending[idx].pending, req);
        }
    }
    rwl_unwrite(&gl->path_chunk.pending_lock);
    if (path.distance != -1) {
        send_packet(client_by_id(gl, path.client)->client, packet, len);
    }
}

int send_chunk_body(GlobalContext *gl, ChunkId chunk, RequestType type, const void *body, i64 body_len) {
    rwl_write(&gl->path_chunk.pending_lock);
    Path path = get_path(gl, chunk);
    if (path.distance == -1) {
        int idx = hmgeti(gl->path_chunk.pending, chunk);
        i64 total_size = sizeof(RequestHeader) + body_len;
        PendingRequest req = {.packet = malloc(total_size), .size = total_size};
        memcpy(req.packet + sizeof(RequestHeader), body, body_len);
        RequestHeader *hdr = req.packet;
        generate_guid(hdr->guid.guid);
        hdr->type = type;
        hdr->size = body_len;
        if (idx == -1) {
            PendingUnicastKeyValue value = {
                .key = chunk,
                .pending = NULL,
            };
            arrput(value.pending, req);
            hmputs(gl->path_chunk.pending, value);
        } else {
            arrput(gl->path_chunk.pending[idx].pending, req);
        }
    }
    rwl_unwrite(&gl->path_chunk.pending_lock);
    if (path.distance != -1) {
        send_body(client_by_id(gl, path.client)->client, type, body, body_len);
    }
}
