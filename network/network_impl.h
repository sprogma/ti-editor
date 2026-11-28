#ifndef NETWORK_IMPL_H
#define NETWORK_IMPL_H

#include "network.h"

#include <msquic.h>

#include <api.h>
#include <ds.h>
#include <mp.h>

STRUCT (Guid) {
    byte value[16];
};

struct Channel;
typedef struct Channel Channel;

STRUCT (PeerStat) {
    double idle_power;
    u64 free_ram;
    u64 free_disk;
};

#define max_hops 5
#define abf_size 1024
#define abf_arity 3

STRUCT (PeerFilter) {
    u64 *filter;
    u64 *second_buffer;
    Time last_rebuild;
};

STRUCT (Peer) {
    NetworkNode id;
    HQUIC conn;
    _Atomic (Channel *) ctrl_stream;
    rwlock ctrl_stream_init;

    peer_addr_t addr;
    bool is_incoming;
    _Atomic i32 is_connected;
    _Atomic i32 refcount;

    PeerStat stat;

    PeerFilter abf[max_hops];
};

STRUCT (Channel) {
    Peer *peer;
    HQUIC stream;
    bool is_framed; // add 4 byte size header ?
    bool is_handshake_pending;
    _Atomic i32 is_closed;
    _Atomic i32 refcount;

    byte *rx_buf; // stb_ds buffer for chunks
    u32 rx_offset;
};

typedef void (*net_on_message_fn)(Channel *ch, const byte *data, u32 len);

STRUCT (OperationEvent) {
    // if target == Self -> target is this computer
    NetworkNode target;
};

extern struct NetworkContext {
    // network items
    const QUIC_API_TABLE *api;
    HQUIC registration;
    HQUIC server_cfg;
    HQUIC client_cfg;
    HQUIC listener;

    net_on_message_fn on_message;

    u16 local_port;
    rwlock peers_lock;
    struct NetworkNodeKV {
        NetworkNode key;
        Peer *value;
    } *peers;
    NetworkNode self_id;

    // operations
    struct OperationKV {
        OperationId key;
        OperationEvent *value;
    } *operation_events;
    rwlock operation_lock;

    // guids
    rwlock guid_lock;
    struct GuidKey {
        Guid key;
    } *guid_set;
    struct GuidTimedKey {
        Guid guid;
        Time expires;
    } *guid_buffer;
    u64 guid_buffer_tail;
    u64 guid_buffer_head;
    u64 guid_buffer_size;
    Time guid_expiration_period;

    // stats
    _Atomic i32 stat_running;
    PeerStat stat;
    rwlock stat_local_lock;
    thread_t stat_thread;
    Time stat_share_interval;

    // tasks
    struct GuidTaskKey {
        Guid key;
        u64 tasks_len;
        TaskId *tasks;
    } *task_events;
    rwlock task_lock;

} network_ctx;

void shutdown_network_subsystem(void);

i32 net_connect_peer(const char *ip, u16 port);

i32 net_test_echo(const char *ip, u16 port, const char *message);

void net_set_message_handler(net_on_message_fn handler);

void fill_network_part(NetworkStat *stat, Peer *peer);

///                   |
/// nearly stable api |
///                   V

Channel *get_control_channel(Peer *peer);
Channel *get_control_channel_by_id(NetworkNode id);
i32 channel_send(Channel *ch, const void *data, u32 len, bool finalizate);
Channel *open_channel(Peer *peer, bool is_framed);
void build_operation(OperationTypeId tid, OperationId op_id, void *data, u64 data_size);
Peer *get_peer(NetworkNode id);
void on_incoming_message(Channel *ch, const byte *data, u32 len);
void send_request_operation(OperationId op_id, byte guid[16]);
void share_statistic_worker(void *arg);

bool has_or_remember_guid(Guid value);

bool ref_peer(Peer *peer);
void unref_peer(Peer *peer);
bool ref_channel(Channel *channel);
void unref_channel(Channel *channel);

#endif // NETWORK_IMPL_H
