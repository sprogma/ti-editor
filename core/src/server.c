#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <uv.h>

#include <ds.h>
#include <request.h>
#include <completers.h>
#include <types.h>

#define NTOHS(val) ((((val) & 0xFF00) >> 8) | (((val) & 0x00FF) << 8))

void get_client_name(uv_stream_t* client, char* buffer, i64 buffer_len) {
    if (!client || !buffer || buffer_len <= 0) return;

    if (client->type == UV_TCP) {
        struct sockaddr_storage peername;
        int namelen = sizeof(peername);
        
        if (uv_tcp_getpeername((uv_tcp_t*)client, (struct sockaddr*)&peername, &namelen) == 0) {
            char ip[INET6_ADDRSTRLEN] = {0};
            int port = 0;
            
            if (peername.ss_family == AF_INET) {
                struct sockaddr_in* addrin = (struct sockaddr_in*)&peername;
                uv_ip4_name(addrin, ip, sizeof(ip));
                port = NTOHS(addrin->sin_port);
                snprintf(buffer, buffer_len, "%s:%d", ip, port);
            } else if (peername.ss_family == AF_INET6) {
                struct sockaddr_in6* addrin6 = (struct sockaddr_in6*)&peername;
                uv_ip6_name(addrin6, ip, sizeof(ip));
                port = NTOHS(addrin6->sin6_port);
                snprintf(buffer, buffer_len, "[%s]:%d", ip, port);
            } else {
                snprintf(buffer, buffer_len, "unknown-tcp");
            }
        } else {
            snprintf(buffer, buffer_len, "error-tcp-getpeername");
        }
    } 
    else if (client->type == UV_NAMED_PIPE) {
        i64 prefix_len = 7;
        if (buffer_len <= prefix_len + 1) {
            snprintf(buffer, buffer_len, "err");
            return;
        }

        memcpy(buffer, "pipe://", prefix_len);
        size_t name_len = (size_t)(buffer_len - prefix_len); 

        int r = uv_pipe_getpeername((uv_pipe_t*)client, buffer + prefix_len, &name_len);
        if (r == 0) {
            i64 total_len = prefix_len + (i64)name_len;
            if (total_len < buffer_len) {
                buffer[total_len] = '\0';
            } else {
                buffer[buffer_len - 1] = '\0';
            }
        } else {
            snprintf(buffer, buffer_len, "error-pipe-getpeername");
        }
    } else {
        snprintf(buffer, buffer_len, "unsupported-stream-type");
    }
}

typedef struct {
    RequestGuid key;
    Ticks expires;
} GuidTableItem;

GuidTableItem *guid_table;

void alloc_buffer(uv_handle_t* handle, u64 suggested_size, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
}

typedef struct {
    uv_write_t req;
    i64 bytes;
    i64 requests;
    char *base;
} SendReq;

static void on_write(uv_write_t *req, int status) {
    SendReq *sreq = (SendReq *)req;
    uv_stream_t *client = req->handle;
    ClientContext *ctx = client ? (ClientContext *)client->data : NULL;

    if (status == 0) {
        if (ctx) {
            ctx->metrics.bytes_sent    += sreq->bytes;
            ctx->metrics.requests_sent += sreq->requests;
        }
    } else {
        fprintf(stderr, "Write error: %s\n", uv_strerror(status));
    }

    free(sreq->base);
    free(sreq);
}

static int queue_write(uv_stream_t *client, char *data, i64 len, i64 requests) {
    SendReq *sreq = malloc(sizeof(SendReq));
    if (!sreq) { free(data); return UV_ENOMEM; }

    sreq->bytes    = len;
    sreq->requests = requests;
    sreq->base     = data;

    uv_buf_t wrbuf = uv_buf_init(data, (unsigned int)len);
    int r = uv_write(&sreq->req, client, &wrbuf, 1, on_write);
    if (r < 0) {
        free(data);
        free(sreq);
    }
    return r;
}

static void generate_guid(byte guid[16]) {
    static u64 counter = 0;
    u64 s = uv_hrtime()
          ^ (u64)(uintptr_t)&counter
          ^ (counter++)
          ^ ((u64)(unsigned)rand() << 32)
          ^ (u64)(unsigned)rand();
    for (int i = 0; i < 16; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        guid[i] = (byte)(s & 0xFF);
    }
}

int send_bytes(uv_stream_t *client, const void *data, i64 len, i64 requests) {
    if (!client)                  return UV_EINVAL;
    if (len < 0)                  return UV_EINVAL;
    if (len > 0 && !data)         return UV_EINVAL;
    if (len > MAX_REQUEST_SIZE)   return UV_EINVAL;

    char *copy = malloc((size_t)(len > 0 ? len : 1));
    if (!copy) return UV_ENOMEM;
    if (len > 0) memcpy(copy, data, (size_t)len);

    return queue_write(client, copy, len, requests);
}

int send_packet(uv_stream_t *client, const void *packet, i64 len) {
    return send_bytes(client, packet, len, 1);
}

int send_body(uv_stream_t *client, RequestType type,
              const void *body, i64 body_len) {
    if (!client)                                    return UV_EINVAL;
    if (body_len < 0)                               return UV_EINVAL;
    if (body_len > 0 && !body)                      return UV_EINVAL;
    if (body_len > MAX_REQUEST_SIZE - (i64)sizeof(RequestHeader))
        return UV_EINVAL;

    RequestHeader hdr;
    generate_guid(hdr.guid.guid);
    hdr.type = type;
    hdr.size = body_len;
    
    int r = send_bytes(client, &hdr, (i64)sizeof(hdr), 1);
    if (r < 0) return r;
    if (body_len > 0) {
        r = send_bytes(client, body, body_len, 0);
        if (r < 0) return r;
    }
    return 0;
}

void on_client_close(uv_handle_t *handle) {
    ClientContext *ctx = handle->data;
    if (ctx) {
        printf("Client %s disconnected "
               "(sent: %lld B / %lld reqs, recv: %lld B / %lld reqs)\n",
               ctx->client_name,
               (long long)ctx->metrics.bytes_sent,
               (long long)ctx->metrics.requests_sent,
               (long long)ctx->metrics.bytes_received,
               (long long)ctx->metrics.requests_received);
        complete_cleanup(ctx);
        free(ctx);
    }
    free(handle);
}

void on_read(uv_stream_t* client, i64 nread, const uv_buf_t* buf) {
    if (nread > 0) {
        assert(buf->base);

        ClientContext *ctx = client->data;
        ctx->metrics.bytes_received += nread;

        i64 offset = 0;
        while (offset < nread) {
            i64 to_read = min(ctx->length - ctx->read, nread - offset);
            memcpy(ctx->buffer + ctx->read, buf->base, to_read);
            ctx->read += to_read;
            offset += to_read;

            if (ctx->read == ctx->length) {
                if (ctx->type == RequestType_Incomplete) {
                    RequestHeader *header = (RequestHeader *)ctx->buffer;
                    i64 index = hmgeti(guid_table, header->guid);
                    if (index != -1 && get_ticks() < guid_table[index].expires) {
                        ctx->type = RequestType_IgnoreRepeat;
                    } else {
                        GuidTableItem item = {
                            header->guid,
                            timeout_from_now(REQUEST_GUID_TTL),
                        };
                        hmputs(guid_table, item);
                        ctx->type = header->type;
                    }
                    ctx->length = header->size;
                    ctx->read = 0;
                }
                // allow messages with body size 0
                if (ctx->type != RequestType_Incomplete && ctx->read == ctx->length) {
                    ctx->metrics.requests_received += 1;
                    if (ctx->type != RequestType_IgnoreRepeat) {
                        ctx->metrics.unique_requests_received += 1;
                        complete_request(client, ctx);
                    }
                    ctx->type = RequestType_Incomplete;
                    ctx->length = sizeof(RequestHeader);
                    ctx->read = 0;
                }
            }
        }
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Read error: %s\n", uv_strerror(nread));
        }
        uv_close((uv_handle_t*) client, on_client_close);
    }
    
    if (buf->base) {
        free(buf->base);
    }
}

void on_new_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error: %s\n", uv_strerror(status));
        return;
    }

    uv_stream_t* client = NULL;

    if (server->type == UV_TCP) {
        client = (uv_stream_t*)malloc(sizeof(uv_tcp_t));
        uv_tcp_init(server->loop, (uv_tcp_t*)client);
    } else if (server->type == UV_NAMED_PIPE) {
        client = (uv_stream_t*)malloc(sizeof(uv_pipe_t));
        uv_pipe_init(server->loop, (uv_pipe_t*)client, 0);
    }

    if (client && uv_accept(server, client) == 0) {
        char name[512];
        get_client_name(client, name, sizeof(name));
        client->data = complete_connection(client, name);
        uv_read_start(client, alloc_buffer, on_read);
    } else {
        if (client) {
            uv_close((uv_handle_t*) client, on_client_close);
        }
    }
}

#define BACKLOG 128

int main(int argc, char** argv) {
    init_types();
    
    uv_loop_t *loop = uv_default_loop();
    int servers_started = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            int port = atoi(argv[++i]);
            if (port <= 0) {
                fprintf(stderr, "Invalid port value\n");
                continue;
            }

            uv_tcp_t* tcp_server = malloc(sizeof(uv_tcp_t));
            uv_tcp_init(loop, tcp_server);

            struct sockaddr_in6 addr;
            uv_ip6_addr("::", port, &addr);

            int r = uv_tcp_bind(tcp_server, (const struct sockaddr*)&addr, 0);
            if (r < 0) {
                fprintf(stderr, "TCP Bind error for port %d: %s\n", port, uv_strerror(r));
                free(tcp_server);
                continue;
            }

            r = uv_listen((uv_stream_t*)tcp_server, BACKLOG, on_new_connection);
            if (r) {
                fprintf(stderr, "TCP Listen error for port %d: %s\n", port, uv_strerror(r));
                free(tcp_server);
                continue;
            }

            printf("Listening on TCP port %d...\n", port);
            servers_started++;
        } 
        else if (strcmp(argv[i], "--pipe") == 0 && i + 1 < argc) {
            char* pipe_path = argv[++i];

            uv_pipe_t* pipe_server = malloc(sizeof(uv_pipe_t));
            uv_pipe_init(loop, pipe_server, 0);

#ifndef _WIN32
            unlink(pipe_path);
#endif

            int r = uv_pipe_bind(pipe_server, pipe_path);
            if (r < 0) {
                fprintf(stderr, "Pipe Bind error for %s: %s\n", pipe_path, uv_strerror(r));
                free(pipe_server);
                continue;
            }

            r = uv_listen((uv_stream_t*)pipe_server, BACKLOG, on_new_connection);
            if (r) {
                fprintf(stderr, "Pipe Listen error for %s: %s\n", pipe_path, uv_strerror(r));
                free(pipe_server);
                continue;
            }

            printf("Listening on Pipe: %s...\n", pipe_path);
            servers_started++;
        }
    }

    if (servers_started == 0) {
        fprintf(stderr, "Error: You must specify at least one valid --port or --pipe\n");
        fprintf(stderr, "Usage: %s [--port <num>] [--pipe <path>]\n", argv[0]);
        return 1;
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}

