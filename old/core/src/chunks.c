#if 0

#include "data.h"

#include "stb_ds.h"
#include "mp.h"

typedef struct {
    ChunkId key;
    i64 local_offset;
} ChunkMapItem;

static struct {
    LocalServer *server;
    i64 max_mem;
    i64 used_mem;
    
    u8 *bytes;
    i64 bytes_size;
    
    Label *labels;
    i64 labels_size;
    
    Chunk *chunks;
    i64 chunks_size;
    i64 *free_chunks;
    i64 free_chunks_max;
    i64 free_chunks_size;

    ChunkMapItem *chunk_map;

    rwlock chunk_lock;
    rwlock byte_lock;
    rwlock label_lock;
} Ctx = {};

void init_chunks(LocalServer *server) {
    Ctx.server = server;
    Ctx.max_mem = 64 * 1024 * 1024;
    rwl_init(Ctx.chunk_lock);
    rwl_init(Ctx.byte_lock);
    rwl_init(Ctx.label_lock);
}



u8 *chunk_get_byte(ChunkId id) {
    wrl_read(Ctx.chunk_lock);

    Chunk *chunk = hmget(id);
    u8 *bytes = chunk->data.data;

    wrl_unread(Ctx.chunk_lock);    
}

typedef void (*Callback)(void *)

u8 *chunk_get_bytes(ChunkId id);
Label *chunk_get_labels(ChunkId id);
void chunk_release_bytes(ChunkId id);
void chunk_release_labels(ChunkId id);

// chunk allocator
ChunkId create_chunk(i64 bytes, i64 labels);
void chunk_download(ChunkId id, Callback fn, void *data);
void chunk_ref(ChunkId id);
void chunk_unref(ChunkId id);




static bool reserve_mem(i64 count) {
    i64 res = atomic_fetch_add(&Ctx.used_mem, count);
    if (res + count > Ctx.max_mem) {
        atomic_fetch_add(&Ctx.used_mem, -count);
        return false;
    }
    return true; 
}

#define DEF_REALLOC(chunks) \
static bool realloc_ ## cchunks (i64 new_size) { \
    i64 size_diff = (new_size - Ctx. chunks ## _size) * sizeof(*Ctx. chunks); \
    void *res; \
    if (size_diff > 0) { \
        if (!reserve_mem(size_diff)) { return false; } \
        res = realloc(Ctx. chunks, sizeof(*Ctx. chunks) * new_size); \
        if (!res) { reserve_mem(-size_diff); return false; } \
    } else { \
        res = realloc(Ctx. chunks, sizeof(*Ctx. chunks) * new_size); \
        if (!res) { return false; } \
        if (!reserve_mem(size_diff)) { return false; } \
    } \
    Ctx. chunks = res; \
    Ctx. chunks ## _size = new_size; \
    return true; \
}

DEF_REALLOC(bytes)
DEF_REALLOC(labels)
DEF_REALLOC(chunks)

i64 get_free_chunk() {
    if (Ctx.free_chunks_size > 0) {
        return Ctx.free_chunks[--Ctx.free_chunks_size];
    } else if (Ctx.free_chunks_max < Ctx.chunks_size) {
        return Ctx.free_chunks_max++;
    } else {
        if (!realloc_chunks(Ctx.chunks_size * 1.2 + 100) || Ctx.free_chunks_max >= Ctx.chunks_size) {
            return -1;
        }
        return Ctx.free_chunks_max++;
    }
}

ChunkId create_chunk(DataChunk data, LabelChunk label) {
    wrl_write(Ctx.chunk_lock);

    i32 hi_id = Ctx.server->high_chunk_id;
    i32 lo_id = atomic_fetch_add(&Ctx.server->low_chunk_id, 1);
    ChunkId id = (hi_id << 32) | lo_id;

    i64 offset = get_free_chunk();
    if (offset < 0) { return InvalidChunkId; }
    
    hmput(Ctx.chunk_map, id, offset);
    
    Ctx.chunkds[offset].data = data;
    Ctx.chunkds[offset].label = label;
    Ctx.chunkds[offset].refs = 1;
    
    wrl_unwrite(Ctx.chunk_lock);
    return id;
}

void chunk_ref(ChunkId cnk) {
    wrl_read(Ctx.chunk_lock);

    i64 offset = hmget(Ctx.chunk_map, cnk);
    atomic_fetch_add(&Ctx.chunks[offset].refs, 1);
    
    wrl_unread(Ctx.chunk_lock);
}

void chunk_free(ChunkId cnk) {
    wrl_read(Ctx.chunk_lock);
    
    i64 offset = hmget(Ctx.chunk_map, cnk);
    if (atomic_fetch_add(&Ctx.chunks[offset].refs, -1) == 1) {

        Ctx.free_chunks[Ctx.free_chunks_size++] = offset;
        label_ptr_free()

        hmrem(Ctx.chunk_map, cnk);
    }
    
    wrl_unread(Ctx.chunk_lock);
}

Chunk chunk_get(ChunkId cnk) {
    wrl_read(Ctx.chunk_lock);
    
    wrl_unread(Ctx.chunk_lock);
}

BytePtr byte_ptr_allocate(OperationId op, i64 size, i64 alignment) {
    wrl_write(Ctx.byte_lock);
    
    wrl_unwrite(Ctx.byte_lock);
}

void byte_ptr_free(OperationId op, BytePtr ptr, i64 size) {
    wrl_write(Ctx.byte_lock);
    
    wrl_unwrite(Ctx.byte_lock);
}

u8 *byte_ptr_base_address(OperationId op) {
    wrl_read(Ctx.byte_lock);
    
    wrl_unread(Ctx.byte_lock);
}

LabelPtr label_ptr_allocate(OperationId op, i64 size, i64 alignment) {
    wrl_write(Ctx.label_lock);
    
    wrl_unwrite(Ctx.label_lock);
}

void label_ptr_free(OperationId op, LabelPtr ptr, i64 size) {
    wrl_write(Ctx.label_lock);
    
    wrl_unwrite(Ctx.label_lock);
}

Label *label_ptr_base_address(OperationId op) {
    wrl_read(Ctx.label_lock);
    
    wrl_unread(Ctx.label_lock);
}

#endif
