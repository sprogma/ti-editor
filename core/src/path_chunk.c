#include <stdio.h>
#include <uv.h>

#include <ds.h>
#include <request.h>
#include <completers.h>
#include <types.h>


typedef struct {
    ChunkId chunk;
} GetPath;

typedef struct {
    ChunkId chunk;
} AnswerPath;

void answer_path_chunk(uv_stream_t *client, ClientContext *ctx, ChunkId chunk, i64 distance) {
    for (int i = 0; i < clients; ++i) {
        send(clients[i], Request_AnswerPathChunk, chunk, distance);
    }
}

void complete_path_chunk(uv_stream_t *client, ClientContext *ctx) {
    GetPath *data = client->buffer;

    int is_local = hmgeti(local_chunks???, data->chunk);
    if (is_local != -1) {
        answer_path_chunk(client, ctx, data->chunk, 0);
    }
}
