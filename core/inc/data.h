#ifndef CORE_DATA_H
#define CORE_DATA_H

typedef i64 BytePtr;
typedef i64 LabelPtr;
typedef i64 ChunkId;

typedef struct { i32 position; i32 id; } Label;

typedef struct {
    i64 length;
    BytePtr data;
} DataChunk;

typedef struct {
    i64 length;
    LabelPtr data; // labels are sorted by pos inside one chunk
} LabelChunk;

typedef struct {
    DataChunk data;
    LabelChunk label;
} Chunk;


// byte allocator
BytePtr byte_ptr_allocate(OperationId op, i64 size, i64 alignment);
void byte_ptr_free(OperationId op, BytePtr ptr, i64 size);
u8 *byte_ptr_base_address(OperationId op);

// label allocator
BytePtr label_ptr_allocate(OperationId op, i64 size, i64 alignment);
void label_ptr_free(OperationId op, LabelPtr ptr, i64 size);
Label *label_ptr_base_address(OperationId op);

// chunk allocator
ChunkId chunk_allocate(i64 size, i64 alignment);
void chunk_free(ChunkId cnk);
Chunk chunk_get(ChunkId cnk);


#endif // CORE_DATA_H
