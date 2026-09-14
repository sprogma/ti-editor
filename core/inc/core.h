#ifndef CORE_CORE_H
#define CORE_CORE_H

#include "types.h"

/// types:

typedef Ptr TreePtr;
typedef Ptr ChunkPtr;
typedef Ptr StatPtr;
typedef Ptr64 OperationPtr64;

typedef struct {
    i32 priority;
    StatPtr stat;
    Tagged1Ptr l, r; // 0 = TreePtr 1 = ChunkPtr
} Tree;

typedef struct {
    size_t length;
    byte *data;
} DataChunk;

typedef struct {
    size_t length;
    Anchor *data;
} AnchorChunk;

enum {
    ChunkInfoNone = 0
    ChunkInfoLazy = 0b00000001
};

typedef struct {
    i32 info;
    StatPtr stat;
    union {
        struct {
            ChunkPtr from, to;
        } Lazy;
        struct {
            DataChunkPtr data;
            AnchorChunkPtr anchors;
        } Ready;
    };
} Chunk;

typedef Operation {
    RWLock lock;
    Tagged1Ptr cache; // 0 = TreePtr 1 = ChunkPtr
    const void *data;
};

/// api:

typedef i32 Queue;

/// functions:

void request_chunks(Queue dest, OperationPtr operation, Anchor position, i64 offset);

Tagged1Ptr get_cache(OperationPtr operation);
void set_cache(OperationPtr operation, Tagged1Ptr new_cache);


///---------------------------------------------------------------

// To fill:
// * %GET%
// * %EMIT%

//   operation workers:
// running, have `task` and `operation` as input.
// inside it read tasks from given operation using %GET%
// after performing operation, it has some array of chunks.
// it stores them (chunks) in cache, and returns as array
// 
// !! HOW track chunck connectivity?
// - maybe simply copy all chuncks with setting state = Unknown? and then only split and join them?
// - maybe there is no need of copying? can i take prev state tree, and fork form it? - NO becouse it isn't completed to the end

// emits chunks, using %EMIT%

// returns mapped buffer if it is ready, overwice return NULL.
const byte *lock_buffer(state, anchor, offset, length, priority);

// free buffer if it was mapped
void unlock_buffer(const byte *ptr);


#endif // CORE_CORE_H

