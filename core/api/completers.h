#ifndef CORE_API_COMPLETERS_H
#define CORE_API_COMPLETERS_H

#include <types.h>

typedef struct {
    byte guid[16];
} RequestGuid;

typedef struct {
    RequestGuid guid;
    RequestType type;
    i64 size;
} RequestHeader;

#define MAX_REQUEST_SIZE (64 * 1024)
#define REQUEST_GUID_TTL 60000000 // 6 min

typedef struct {
    int client_id;
    char client_name[256];
    RequestType type;
    i64 length;
    i64 read;
    char buffer[MAX_REQUEST_SIZE];
    struct {
        i64 bytes_sent;
        i64 bytes_received;
        i64 requests_sent;
        i64 requests_received;
        i64 unique_requests_received;
    } metrics;
} ClientContext;


// on new connection
ClientContext *complete_connection(uv_stream_t *client, char *name);

// on new request
void complete_request(uv_stream_t *client, ClientContext *ctx);

// on connection closage
void complete_cleanup(ClientContext *ctx);

#endif // CORE_API_COMPLETERS_H
