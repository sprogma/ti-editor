#ifndef NETWORK_H
#define NETWORK_H

#include <api.h>
#include <msquic.h>


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

typedef struct peer_t {
    u64 node_id;
    HQUIC conn;
    channel_t *ctrl_stream;
    peer_addr_t addr;
    bool is_incoming;
    _Atomic i32 is_connected;
} peer_t;

typedef struct channel_t {
    HQUIC stream;
    peer_t *peer;
    bool is_framed; // add 4 byte size header ?
    _Atomic i32 is_closed;

    byte *rx_buf; // stb_ds buffer for chunks
    u32 rx_offset;
} channel_t;


i32 init_network_subsystem(CoreConfig *cfg);
void shutdown_network_subsystem(void);

i32 net_connect_peer(const char *ip, u16 port);

i32 net_test_echo(const char *ip, u16 port, const char *message);

typedef void (*net_on_message_fn)(peer_t *peer, channel_t *ch, const byte *data, u32 len);

void net_set_message_handler(net_on_message_fn handler);

void start_network(void);


#endif // NETWORK_H
