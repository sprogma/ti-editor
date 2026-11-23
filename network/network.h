#ifndef NETWORK_H
#define NETWORK_H

#include <msquic.h>

#include <api.h>
#include <ds.h>
#include <mp.h>


typedef enum {
    PEER_ADDR_DIRECT_IP = 0,
    PEER_ADDR_NAT_PUNCH = 1,
} peer_addr_type_t;

typedef struct {
    peer_addr_type_t type;
    union {
        struct {
            char ip[64];
            u16 port;
        } direct;
        struct {
            u64 session_id;
            char ext_ip[64];
            u16 ext_port;
            u16 local_port;
        } nat;
    };
} peer_addr_t;

struct channel_t;
typedef struct channel_t channel_t;

STRUCT (PeerStat) {
    double idle_power;
    u64 free_ram;
    u64 free_disk;
};  

STRUCT (Peer) {
    NetworkNode id;
    HQUIC conn;
    channel_t *ctrl_stream;
    peer_addr_t addr;
    bool is_incoming;
    _Atomic i32 is_connected;

    PeerStat last_stat;
};

typedef struct channel_t {
    HQUIC stream;
    Peer *peer;
    bool is_framed; // add 4 byte size header ?
    _Atomic i32 is_closed;

    byte *rx_buf; // stb_ds buffer for chunks
    u32 rx_offset;
} channel_t;

typedef void (*net_on_message_fn)(Peer *peer, channel_t *ch, const byte *data, u32 len);

#define NetworkNodeSelf 0xFFFFFFFFFFFFFFFFull

STRUCT (NetworkNodeKV) {
    NetworkNode key;
    Peer *value;
};

extern struct NetworkContext {
    const QUIC_API_TABLE *api;
    HQUIC registration;
    HQUIC server_cfg;
    HQUIC client_cfg;
    HQUIC listener;
    
    net_on_message_fn on_message;

    u16 local_port;
    rwlock peers_lock;
    NetworkNodeKV *peers;
    NetworkNode self_id;
} network_ctx;

i32 init_network_subsystem(CoreConfig *cfg);
void shutdown_network_subsystem(void);

i32 net_connect_peer(const char *ip, u16 port);

i32 net_test_echo(const char *ip, u16 port, const char *message);

void net_set_message_handler(net_on_message_fn handler);

void start_network(void);

void fill_network_part(NetworkStat *stat, Peer *peer);

#endif // NETWORK_H
