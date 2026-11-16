#ifndef CORE_API_COMPLETERS_H
#define CORE_API_COMPLETERS_H

#include <mp.h>
#include <types.h>
#include <request.h>
#include <data.h>

typedef struct {
    byte guid[16];
} RequestGuid;

typedef struct {
    RequestGuid guid;
    RequestType type;
    i64 size;
} RequestHeader;

// todo make dynamic
#define MAX_CLIENTS 128

// todo read from file
#define REQUEST_GUID_TTL 60000000 // 6 min

// may be ok
#define MAX_REQUEST_SIZE (64 * 1024)

struct GlobalContext;

typedef i32 ClientId;

typedef struct ClientContext {
    ClientId client_id;
    struct GlobalContext *global;
    uv_stream_t *client;
    RequestType type;
    i64 length;
    i64 read;
    struct {
        i64 bytes_sent;
        i64 bytes_received;
        i64 requests_sent;
        i64 requests_received;
        i64 unique_requests_received;
    } metrics;
    char client_name[256];
    char buffer[MAX_REQUEST_SIZE];
} ClientContext;

typedef struct PathChunkKeyValue {
    ChunkId key;
    ClientId client;
    i32 distance;
    Time expires;
} PathChunkKeyValue;

typedef struct PendingRequest {
    void *packet;
    i64 size;
} PendingRequest;

typedef struct PendingUnicastKeyValue {
    ChunkId key; // wait path to this
    PendingRequest *pending;
} PendingUnicastKeyValue;

typedef struct GlobalContext {
    struct {
        // full client list
        ClientContext *data[MAX_CLIENTS];
        i64 count;
        rwlock lock;
    } clients;
    struct {
        // chunk -> node mapping
        LocalChunkKeyValue *local;
        rwlock lock;
    } chunks;
    struct {
        // chunk -> path to it
        PathChunkKeyValue *data;
        rwlock lock;
        // chunk -> array of pending requests
        PendingUnicastKeyValue *pending;
        rwlock pending_lock; // must be taken BEFORE path_chunk.lock
    } path_chunk;
} GlobalContext;


// on new connection
ClientContext *complete_connection(uv_stream_t *client, char *name);

// on new request
void complete_request(ClientContext *ctx);

// handlers
void complete_path_chunk(ClientContext *ctx);

// on connection closage
void complete_cleanup(ClientContext *ctx);

// helpers
int send_bytes(uv_stream_t *client, const void *data, i64 len, i64 requests);
int send_packet(uv_stream_t *client, const void *packet, i64 len);
int send_body(uv_stream_t *client, RequestType type, const void *body, i64 body_len);

void generate_guid(byte guid[16]);
int send_chunk_packet(GlobalContext *gl, ChunkId chunk, const void *packet, i64 len);
int send_chunk_body(GlobalContext *gl, ChunkId chunk, RequestType type, const void *body, i64 body_len);

ClientContext *client_by_id(GlobalContext *gl, ClientId client);

#endif // CORE_API_COMPLETERS_H
