#ifndef CORE_API_DATA_H
#define CORE_API_DATA_H

#include "stdatomic.h"

typedef void *BytePtr;
typedef void *LabelPtr;
typedef i64 ChunkId;

#define InvalidChunkId -1

typedef struct { i32 position; i32 id; } Label;

typedef enum { 
    ChunkState_InMemory = 0x0000,
    ChunkState_AtDisk   = 0x0001,
} ChunkState;

typedef struct {
    _Atomic ChunkState state;
    _Atomic int refs;
    i64 length;
    BytePtr data;
} DataChunk;

typedef struct {
    _Atomic ChunkState state;
    _Atomic int refs;
    i64 length;
    LabelPtr data; // labels are sorted by pos inside one chunk
} LabelChunk;

typedef struct {
    DataChunk data;
    LabelChunk label;
    _Atomic int refs;
} Chunk;

typedef enum {
    ChunkInfoState_LabelsInMemory = 0x0001,
    ChunkInfoState_BytesInMemory  = 0x0002,
    ChunkInfoState_Remote         = 0x0004,
} ChunkInfoState;

typedef struct {
    i64 bytes;
    i64 labels;
    ChunkInfoState
} ChunkInfo;

typedef void (*DownloadCallback)(bool result, void *data);
typedef void (*RefCallback)(bool result, void *data);

// access to items
u8 *chunk_get_bytes(ChunkId id); // may return NULL, no release needed
Label *chunk_get_labels(ChunkId id); // may return NULL, no release needed
void chunk_release_bytes(ChunkId id);
void chunk_release_labels(ChunkId id);

// chunk allocator
ChunkId create_chunk(i64 bytes, i64 labels); // may return ChunkIdInvalid
enum { Chunk_DownloadPending; Chunk_DownloadCompleted; } chunk_download(ChunkId id);
void chunk_ref(ChunkId id);
void chunk_unref(ChunkId id);


#endif // CORE_API_DATA_H
