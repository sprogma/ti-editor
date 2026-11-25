#include "network_impl.h"
#include <config_impl.h>
#include <mp.h>
#include <ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <msquic.h>

#define ctx network_ctx

static const QUIC_BUFFER Alpn = { sizeof("editor-mesh") - 1, (uint8_t *)"editor-mesh" };

#define MAX_FRAMED_MSG_SIZE (16 * 1024 * 1024)

typedef struct {
    QUIC_BUFFER quic_buf;
    byte data[];
} send_ctx_t;

static QUIC_STATUS QUIC_API channel_callback(HQUIC stream, void *context, QUIC_STREAM_EVENT *event);

static void register_peer(Peer *peer) {
    rwl_write(&ctx.peers_lock);
    hmput(ctx.peers, peer->id, peer);
    rwl_unwrite(&ctx.peers_lock);
}

static void unregister_peer(Peer *peer) {
    rwl_write(&ctx.peers_lock);
    hmdel(ctx.peers, peer->id);
    rwl_unwrite(&ctx.peers_lock);
}

static void stream_send_raw(HQUIC stream, const void *data, u32 len, bool fin) {
    send_ctx_t *sctx = malloc(sizeof(send_ctx_t) + len);
    if (!sctx) return;

    memcpy(sctx->data, data, len);
    sctx->quic_buf.Buffer = (uint8_t *)sctx->data;
    sctx->quic_buf.Length = len;

    QUIC_SEND_FLAGS flags = fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;
    QUIC_STATUS status = ctx.api->StreamSend(stream, &sctx->quic_buf, 1, flags, sctx);
    if (QUIC_FAILED(status)) {
        printf("[Net] StreamSend failed: 0x%lx\n", (unsigned long)status);
        free(sctx);
    }
}

static QUIC_STATUS QUIC_API stream_callback(HQUIC stream, void *context, QUIC_STREAM_EVENT *event) {
    Peer *peer = (Peer *)context;

    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            for (u32 i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER *buf = &event->RECEIVE.Buffers[i];
                printf("[Net][%s] Recv (%u bytes): %.*s\n",
                       peer->is_incoming ? "IN" : "OUT",
                       buf->Length, buf->Length, (const char *)buf->Buffer);

                if (peer->is_incoming) {
                    bool is_fin = (event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) &&
                                  (i == event->RECEIVE.BufferCount - 1);
                    stream_send_raw(stream, buf->Buffer, buf->Length, is_fin);
                }
            }
            break;
        }
        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            free(event->SEND_COMPLETE.ClientContext);
            break;
        }
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
            ctx.api->StreamClose(stream);
            break;
        }
        default:
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

// graceful -> wait for 100% data received (FIN)
void close_channel(channel_t *ch, bool graceful) {
    if (!ctx.api || !ch || !ch->stream) {
        return;
    }

    i32 expected = false;
    if (!atomic_compare_exchange_strong(&ch->is_closed, &expected, true)) {
        return;
    }

    QUIC_STREAM_SHUTDOWN_FLAGS flags = graceful
        ? QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL
        : QUIC_STREAM_SHUTDOWN_FLAG_ABORT;

    ctx.api->StreamShutdown(ch->stream, flags, 0);
}

void close_connection(Peer *peer, u64 error_code) {
    if (!ctx.api || !peer || !peer->conn) {
        return;
    }

    ctx.api->ConnectionShutdown(peer->conn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, error_code);
}

static QUIC_STATUS QUIC_API connection_callback(HQUIC connection, void *context, QUIC_CONNECTION_EVENT *event) {
    Peer *peer = (Peer *)context;

    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            atomic_store(&peer->is_connected, true);
            register_peer(peer);
            printf("[Net][%s] Peer connected (total peers: %td)\n",
                   peer->is_incoming ? "IN" : "OUT", hmlen(ctx.peers));
            break;
        }
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            if (!ref_peer(peer)) {
                return QUIC_STATUS_ABORTED;
            }
            channel_t *ch = calloc(1, sizeof(channel_t));
            if (!ch) {
                unref_peer(peer);
                return QUIC_STATUS_OUT_OF_MEMORY;
            }

            ch->stream = event->PEER_STREAM_STARTED.Stream;
            ch->peer = peer;
            ch->is_framed = true;
            atomic_init(&ch->is_closed, false);
            atomic_init(&ch->refcount, 1);

            ctx.api->SetCallbackHandler(ch->stream, (void *)channel_callback, ch);
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT: {
            printf("[Net][%s] Transport shutdown: 0x%lx\n",
                   peer->is_incoming ? "IN" : "OUT",
                   (unsigned long)event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
            printf("[Net][%s] Connection closed\n", peer->is_incoming ? "IN" : "OUT");
            unref_peer(peer);
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER: {
            printf("[Net][%s] Peer closed connection (code: %llu)\n",
                   peer->is_incoming ? "IN" : "OUT",
                   (unsigned long long)event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
            break;
        }
        default:
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

static void init_peer(Peer *peer) {
    atomic_init(&peer->refcount, 1);
    atomic_init(&peer->is_connected, false);
    peer->id = rand(); // TODO: better random / ip based value
    peer->last_stat.idle_power = 1.0;
    peer->last_stat.free_ram = -1;
    peer->last_stat.free_disk = -1;
}

static QUIC_STATUS QUIC_API listener_callback(HQUIC listener, void *context, QUIC_LISTENER_EVENT *event) {
    (void)listener;
    (void)context;

    if (event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        Peer *peer = calloc(1, sizeof(Peer));
        if (!peer) return QUIC_STATUS_OUT_OF_MEMORY;

        peer->conn = event->NEW_CONNECTION.Connection;
        peer->is_incoming = true;

        init_peer(peer);

        ctx.api->SetCallbackHandler(peer->conn, (void *)connection_callback, peer);
        QUIC_STATUS status = ctx.api->ConnectionSetConfiguration(peer->conn, ctx.server_cfg);
        if (QUIC_FAILED(status)) {
            unref_peer(peer);
            return status;
        }
        return QUIC_STATUS_SUCCESS;
    }
    return QUIC_STATUS_NOT_SUPPORTED;
}


#include "certificate.inc"

static QUIC_STATUS init_configurations(u64 idle_timeout_ms) {
    QUIC_STATUS status;
    QUIC_SETTINGS settings = { 0 };

    settings.IdleTimeoutMs = idle_timeout_ms;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.KeepAliveIntervalMs = 15000;
    settings.IsSet.KeepAliveIntervalMs = TRUE;
    settings.PeerBidiStreamCount = 1024;
    settings.IsSet.PeerBidiStreamCount = TRUE;

    // client config
    status = ctx.api->ConfigurationOpen(ctx.registration, &Alpn, 1, &settings, sizeof(settings), NULL, &ctx.client_cfg);
    if (QUIC_FAILED(status)) return status;

    QUIC_CREDENTIAL_CONFIG client_cred = { 0 };
    client_cred.Type = QUIC_CREDENTIAL_TYPE_NONE;
    client_cred.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    status = ctx.api->ConfigurationLoadCredential(ctx.client_cfg, &client_cred);
    if (QUIC_FAILED(status)) return status;

    // server config
    status = ctx.api->ConfigurationOpen(ctx.registration, &Alpn, 1, &settings, sizeof(settings), NULL, &ctx.server_cfg);
    if (QUIC_FAILED(status)) return status;

    QUIC_CERTIFICATE_PKCS12 pkcs12 = {
        .Asn1Blob = (uint8_t *)dev_cert_p12,
        .Asn1BlobLength = sizeof(dev_cert_p12),
        .PrivateKeyPassword = NULL
    };

    QUIC_CREDENTIAL_CONFIG server_cred = { 0 };
    server_cred.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;
    server_cred.CertificatePkcs12 = &pkcs12;
    server_cred.Flags = QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    status = ctx.api->ConfigurationLoadCredential(ctx.server_cfg, &server_cred);
    if (QUIC_FAILED(status)) return status;

    return QUIC_STATUS_SUCCESS;
}


i32 init_network_subsystem(CoreConfig *cfg) {
    QUIC_STATUS status;
    rwl_init(&ctx.peers_lock);

    // TODO: use some better random and device-based id
    ctx.self_id = rand();

    if (QUIC_FAILED(status = MsQuicOpen2(&ctx.api))) {
        printf("[Net] Error MsQuicOpen2: 0x%lx\n", (unsigned long)status);
        return 1;
    }

    const QUIC_REGISTRATION_CONFIG reg_config = { "mesh_node", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    if (QUIC_FAILED(status = ctx.api->RegistrationOpen(&reg_config, &ctx.registration))) {
        printf("[Net] Error RegistrationOpen: 0x%lx\n", (unsigned long)status);
        goto error;
    }

    i64 *port_arr    = get_int(cfg, "net.port");
    i64 *timeout_arr = get_int(cfg, "net.idle_timeout_ms");

    u16 listen_port     = (arrlen(port_arr) > 0)    ? (u16)port_arr[0]    : 4567;
    u64 idle_timeout_ms = (arrlen(timeout_arr) > 0) ? (u64)timeout_arr[0] : 60000;

    ctx.local_port = listen_port;

    if (QUIC_FAILED(status = init_configurations(idle_timeout_ms))) {
        printf("[Net] Error init_configurations: 0x%lx\n", (unsigned long)status);
        goto error;
    }

    if (ctx.server_cfg && listen_port > 0) {
        if (QUIC_FAILED(status = ctx.api->ListenerOpen(ctx.registration, listener_callback, NULL, &ctx.listener))) {
            printf("[Net] Error ListenerOpen: 0x%lx\n", (unsigned long)status);
            goto error;
        }


        QUIC_ADDR addr = { 0 };
        QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_INET);
        QuicAddrSetPort(&addr, listen_port);
        if (QUIC_FAILED(status = ctx.api->ListenerStart(ctx.listener, &Alpn, 1, &addr))) {
            printf("[Net] Error ListenerStart: 0x%lx\n", (unsigned long)status);
            goto error;
        }
        printf("[Net] Listening on UDP port %u\n", listen_port);
    }
    printf("[Net] Network subsystem initialized\n");
    return 0;
error:
    shutdown_network_subsystem();
    return 1;
}

void shutdown_network_subsystem(void) {
    if (!ctx.api) return;
    if (ctx.listener) {
        ctx.api->ListenerClose(ctx.listener);
        ctx.listener = NULL;
    }
    if (ctx.registration) {
        ctx.api->RegistrationShutdown(ctx.registration, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
    }
    if (ctx.server_cfg) {
        ctx.api->ConfigurationClose(ctx.server_cfg);
        ctx.server_cfg = NULL;
    }
    if (ctx.client_cfg) {
        ctx.api->ConfigurationClose(ctx.client_cfg);
        ctx.client_cfg = NULL;
    }
    if (ctx.registration) {
        ctx.api->RegistrationClose(ctx.registration);
        ctx.registration = NULL;
    }
    MsQuicClose(ctx.api);
    ctx.api = NULL;
    rwl_write(&ctx.peers_lock);
    hmfree(ctx.peers);
    rwl_unwrite(&ctx.peers_lock);
    rwl_destroy(&ctx.peers_lock);
    printf("[Net] Network subsystem stopped\n");
}


Peer *open_connection(const peer_addr_t *addr) {
    if (!ctx.api || !ctx.client_cfg || !addr) {
        return NULL;
    }

    Peer *peer = calloc(1, sizeof(Peer));
    if (!peer) {
        return NULL;
    }

    peer->addr = *addr;
    peer->is_incoming = false;

    init_peer(peer);

    QUIC_STATUS status = ctx.api->ConnectionOpen(
        ctx.registration,
        connection_callback,
        peer,
        &peer->conn
    );
    if (QUIC_FAILED(status)) {
        printf("[Net] ConnectionOpen failed: 0x%lx\n", (unsigned long)status);
        unref_peer(peer);
        return NULL;
    }

    switch (addr->type) {
        case PEER_ADDR_DIRECT_IP: {
            QUIC_ADDRESS_FAMILY family = strchr(addr->direct.ip, ':')
                ? QUIC_ADDRESS_FAMILY_INET6
                : QUIC_ADDRESS_FAMILY_INET;

            status = ctx.api->ConnectionStart(
                peer->conn,
                ctx.client_cfg,
                family,
                addr->direct.ip,
                addr->direct.port
            );
            break;
        }

        case PEER_ADDR_NAT_PUNCH: {
            // TODO: NAT punching [STUN/etc]
            // bind peer->conn to nat.local_port, via ctx.api->SetParam(peer->conn, QUIC_PARAM_CONN_LOCAL_ADDRESS, ...)
            // send stun / empty udp to addr->nat.ext_ip, addr->nat.ext_port
            // run quic via ctx.api->ConnnectionStart(...)
            printf("[Net] NAT punch not implemented yet (session: %llu)\n",
                   (unsigned long long)addr->nat.session_id);
            status = QUIC_STATUS_NOT_SUPPORTED;
            break;
        }

        default:
            status = QUIC_STATUS_INVALID_PARAMETER;
            break;
    }

    if (QUIC_FAILED(status)) {
        printf("[Net] ConnectionStart failed: 0x%lx\n", (unsigned long)status);
        ctx.api->ConnectionClose(peer->conn);
        free(peer);
        return NULL;
    }
    return peer;
}

void net_set_message_handler(net_on_message_fn handler) {
    ctx.on_message = handler;
}

static void process_incoming_bytes(channel_t *ch, const byte *data, u32 len) {
    if (!ch->is_framed) {
        if (ctx.on_message) {
            ctx.on_message(ch->peer, ch, data, len);
        }
        return;
    }

    if (len > 0) {
        byte *dst = arraddnptr(ch->rx_buf, len);
        memcpy(dst, data, len);
    }

    while (true) {
        u32 available = (u32)arrlen(ch->rx_buf) - ch->rx_offset;
        if (available < sizeof(u32)) {
            break;
        }

        u32 msg_len = 0;
        memcpy(&msg_len, ch->rx_buf + ch->rx_offset, sizeof(u32));

        if (msg_len > MAX_FRAMED_MSG_SIZE) {
            printf("[Net] Error: message too large (%u bytes), aborting channel\n", msg_len);
            close_channel(ch, false);
            return;
        }

        if (available < sizeof(u32) + msg_len) {
            break;
        }

        const byte *payload = ch->rx_buf + ch->rx_offset + sizeof(u32);
        if (ctx.on_message) {
            ctx.on_message(ch->peer, ch, payload, msg_len);
        }

        ch->rx_offset += sizeof(u32) + msg_len;
    }

    u32 remaining = (u32)arrlen(ch->rx_buf) - ch->rx_offset;
    if (remaining == 0) {
        arrsetlen(ch->rx_buf, 0);
        ch->rx_offset = 0;
    } else if (ch->rx_offset > 65536) {
        memmove(ch->rx_buf, ch->rx_buf + ch->rx_offset, remaining);
        arrsetlen(ch->rx_buf, remaining);
        ch->rx_offset = 0;
    }
}

static QUIC_STATUS QUIC_API channel_callback(HQUIC stream, void *context, QUIC_STREAM_EVENT *event) {
    channel_t *ch = (channel_t *)context;

    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            for (u32 i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER *buf = &event->RECEIVE.Buffers[i];
                process_incoming_bytes(ch, (const byte *)buf->Buffer, buf->Length);
            }
            break;
        }

        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            free(event->SEND_COMPLETE.ClientContext);
            break;
        }

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: {
            close_channel(ch, true);
            break;
        }

        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED: {
            ctx.api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
            break;
        }

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
            unref_channel(ch);
            break;
        }

        default:
            break;
    }
    return QUIC_STATUS_SUCCESS;
}

channel_t *open_channel(Peer *peer, bool is_framed) {
    if (!ctx.api || !peer || !peer->conn) {
        return NULL;
    }

    if (!ref_peer(peer)) {
        return NULL;
    }
    
    channel_t *ch = calloc(1, sizeof(channel_t));
    if (!ch) {
        unref_peer(peer);
        return NULL;
    }

    ch->peer = peer;
    ch->is_framed = is_framed;
    atomic_init(&ch->is_closed, false);
    atomic_init(&ch->refcount, 1);

    QUIC_STATUS status = ctx.api->StreamOpen(
        peer->conn,
        QUIC_STREAM_OPEN_FLAG_NONE, // bidirectional
        channel_callback,
        ch,
        &ch->stream
    );
    if (QUIC_FAILED(status)) {
        printf("[Net] StreamOpen failed: 0x%lx\n", (unsigned long)status);
        unref_channel(ch);
        return NULL;
    }

    status = ctx.api->StreamStart(ch->stream, QUIC_STREAM_START_FLAG_NONE);
    if (QUIC_FAILED(status)) {
        printf("[Net] StreamStart failed: 0x%lx\n", (unsigned long)status);
        ctx.api->StreamClose(ch->stream);
        unref_channel(ch);
        return NULL;
    }

    return ch;
}

// fin = true -> close our side, for one time channels / other closing
i32 channel_send(Peer *peer, channel_t *ch, const void *data, u32 len, bool fin) {
    if (!ctx.api || !peer || !ch) {
        return 1;
    }
    if (len > 0 && !data) {
        return 1;
    }

    if (!ref_channel(ch)) {
        return 1;
    }
    
    u32 header_size = ch->is_framed ? sizeof(u32) : 0;
    u32 total_size  = header_size + len;

    // [send_ctx_t][optional u32 len][data]
    send_ctx_t *sctx = malloc(sizeof(send_ctx_t) + total_size);
    if (!sctx) {
        unref_channel(ch);
        return 1;
    }

    byte *dst = sctx->data;
    if (ch->is_framed) {
        memcpy(dst, &len, sizeof(u32));
        dst += sizeof(u32);
    }

    if (len > 0) {
        memcpy(dst, data, len);
    }

    sctx->quic_buf.Buffer = (uint8_t *)sctx->data;
    sctx->quic_buf.Length = total_size;

    QUIC_SEND_FLAGS flags = fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE;
    QUIC_STATUS status = ctx.api->StreamSend(ch->stream, &sctx->quic_buf, 1, flags, sctx);
    
    unref_channel(ch);
    
    if (QUIC_FAILED(status)) {
        printf("[Net] StreamSend failed: 0x%lx\n", (unsigned long)status);
        free(sctx);
        return 1;
    }

    return 0;
}


void fill_network_part(NetworkStat *stat, Peer *peer) {
    QUIC_NETWORK_STATISTICS nst = { 0 };
    u32 len = sizeof(nst);

    QUIC_STATUS status = ctx.api->GetParam(
        peer->conn,
        QUIC_PARAM_CONN_NETWORK_STATISTICS,
        &len,
        &nst
    );

    if (QUIC_SUCCEEDED(status)) {
        stat->ping = T_US(nst.SmoothedRTT);
        stat->bandwidth = nst.Bandwidth / 8;
    }
}

bool ref_peer(Peer *peer) { // return false -> peer was closed
    if (!peer) return false;
    int count = atomic_load(&peer->refcount);
    while (count > 0) {
        if (!atomic_load(&peer->is_connected)) {
            return false;
        }
        if (atomic_compare_exchange_weak(&peer->refcount, &count, count + 1)) {
            return true;
        }
    }
    return false;
}

void unref_peer(Peer *peer) {
    if (!peer) return;
    if (atomic_fetch_sub_explicit(&peer->refcount, 1, memory_order_release) == 1) {
        atomic_thread_fence(memory_order_acquire);
        
        if (peer->addr.type == PEER_ADDR_DIRECT_IP) {
            printf("Free peer [%s:%d]\n", peer->addr.direct.ip, peer->addr.direct.port);
        } else {
            printf("Free peer ???NAT???\n");
        }
        // TODO: free peer
        // TODO: before freeing we need to implement ref/unref in all places where peer is used, from begining to it's end...        
        if (atomic_load(&peer->is_connected)) {
            // TODO: check consistency of is_connected flag, may be move it's setters inside register/unregister_peer?
            unregister_peer(peer);
        }
        if (peer->conn) {
            ctx.api->ConnectionClose(peer->conn);
            peer->conn = NULL;
        }
        free(peer);
    }
}

bool ref_channel(channel_t *ch) { // return false -> channel was closed
    if (!ch) return false;
    int count = atomic_load(&ch->refcount);
    while (count > 0) {
        if (atomic_load(&ch->is_closed)) {
            return false;
        }
        if (atomic_compare_exchange_weak(&ch->refcount, &count, count + 1)) {
            return true;
        }
    }
    return false;
}

void unref_channel(channel_t *ch) {
    if (!ch) return;
    if (atomic_fetch_sub_explicit(&ch->refcount, 1, memory_order_release) == 1) {
        atomic_thread_fence(memory_order_acquire);
        
        printf("Free channel [peer=%lld]\n", ch->peer->id);
        // TODO: free channel
        // TODO: before freeing we need to implement ref/unref in all places where channel is used, from begining to it's end...
        if (ch->peer && ch->peer->ctrl_stream == ch) {
            // TODO: make this atomic, or guarantee +1 from peer to it's ctrl_stream?
            ch->peer->ctrl_stream = NULL;
        }
        if (ch->stream) {
            ctx.api->StreamClose(ch->stream);
            ch->stream = NULL;
        }
        unref_peer(ch->peer);
        arrfree(ch->rx_buf);
        free(ch);
    }
}
























static channel_t *get_or_open_ctrl_stream(Peer *peer) {
    if (peer->ctrl_stream) {
        return peer->ctrl_stream;
    }

    channel_t *ch = open_channel(peer, true);
    if (ch) {
        peer->ctrl_stream = ch;
    }
    return ch;
}

static void broadcast_message(const char *text) {
    char payload[1024];
    int written = snprintf(payload, sizeof(payload), "[%u]< %s", ctx.local_port, text);
    if (written <= 0) {
        return;
    }

    u32 len = (u32)written;
    if (len >= sizeof(payload)) {
        len = sizeof(payload) - 1;
    }

    rwl_write(&ctx.peers_lock);
    for (u64 i = 0; i < hmlen(ctx.peers); ++i) {
        Peer *p = ctx.peers[i].value;
        if (!atomic_load(&p->is_connected)) {
            continue;
        }

        channel_t *ch = get_or_open_ctrl_stream(p);
        if (ch) {
            channel_send(p, ch, payload, len, false);
        }
    }
    rwl_unwrite(&ctx.peers_lock);
}

static void on_chat_message(Peer *peer, channel_t *ch, const byte *data, u32 len) {
    if (!peer->ctrl_stream) {
        peer->ctrl_stream = ch;
    }

    printf("\n%.*s\n> ", (int)len, (const char *)data);
    fflush(stdout);
}

void start_network(void) {
    net_set_message_handler(on_chat_message);

    printf("[Chat] Node ready on UDP port %u.\n", ctx.local_port);
    printf("Commands:\n");
    printf("  /connect <port>  - connect to 127.0.0.1:<port>\n");
    printf("  /peers           - show connected peers count\n");
    printf("  /quit            - disconnect all and exit\n");
    printf("  <any text>       - broadcast message to all peers via ctrl_stream\n\n");

    char line[1024];
    while (true) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) {
            continue;
        }

        if (strcmp(line, "/quit") == 0) {
            break;
        }

        if (strcmp(line, "/peers") == 0) {
            rwl_read(&ctx.peers_lock);
            printf("[Chat] Active peers: %td\n", hmlen(ctx.peers));
            for (u64 i = 0; i < hmlen(ctx.peers); ++i) {
                Peer *p = ctx.peers[i].value;
                printf("  #%td: %s, connected=%d, ctrl_stream=%p\n",
                       i,
                       p->is_incoming ? "IN" : "OUT",
                       atomic_load(&p->is_connected),
                       (void *)p->ctrl_stream);
            }
            rwl_unread(&ctx.peers_lock);
            continue;
        }

        if (strncmp(line, "/connect ", 9) == 0) {
            u16 target_port = (u16)atoi(line + 9);
            if (target_port == 0) {
                printf("[Chat] Invalid port. Usage: /connect <port>\n");
                continue;
            }

            peer_addr_t addr = {
                .type = PEER_ADDR_DIRECT_IP,
                .direct = {
                    .ip = "127.0.0.1",
                    .port = target_port
                }
            };

            printf("[Chat] Connecting to 127.0.0.1:%u...\n", target_port);
            if (!open_connection(&addr)) {
                printf("[Chat] Failed to start connection\n");
            }
            continue;
        }

        broadcast_message(line);
    }

    rwl_read(&ctx.peers_lock);
    for (u64 i = 0; i < hmlen(ctx.peers); ++i) {
        close_connection(ctx.peers[i].value, 0);
    }
    rwl_unread(&ctx.peers_lock);
}
