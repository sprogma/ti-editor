#ifndef CORE_TREE_H
#define CORE_TREE_H

typedef i64 StatisticId;
typedef i64 StatBlobPtr;

// if TreeNodePtr is leaf, it is ChunkId
typedef u64 TreeNodePtr;
#define TREE_NODE_LEAF (1ull << 63)

#define NEW_BLOB_PTR ((i64)-1)

typedef enum {
    StatResultKind_Integer_Exact,
    StatResultKind_Integer_Range,
    StatResultKind_Integer_Unknown,
    StatResultKind_Boolean_Exact,
    StatResultKind_Boolean_Unknown,
} StatResultKind;

typedef struct {
    StatResultKind kind;
    union {
        struct {
            i64 lo, hi;
            bool lo_inf, hi_inf;
        } integer;
        struct {
            bool value;
        } boolean;
    };
} StatResult;

typedef struct {
    StatTypeId id;
    const char *name;

    // if true, before saving all StatBlobPtr with same value will be stored only once
    // this flag is created to allow optimization, if stat engine is sure that such
    // compression is not needed
    bool use_unique_compression;

    // TODO: may be add void *ctx field... (and parameter to all functions)?
    // void *ctx;

    void (*merge)(StatBlobPtr *dest, StatBlobPtr left, StatBlobPtr right);
    void (*calc)(StatBlobPtr *dest, OperationId op, const Chunk *cnk); // *dest == NEW_BLOB_PTR for new nodes
    void (*copy)(StatBlobPtr *dest, StatBlobPtr blob); // may increase link counter, or copy data
    void (*free)(StatBlobPtr blob);

    i64  (*store_size)(const StatBlobPtr blob);
    void (*store_data)(const StatBlobPtr blob, u8 *dest); // must write exactly store_size(blob) bytes
    bool (*load)(StatBlobPtr *blob, const u8 *source, i64 len); // len will be stored by external api, it is exact result of store_size(blob)

    StatResult (*present)(StatBlobPtr blob); // convert to resulting api format
} StatType;

typedef struct {
    StatTypeId id;
    StatBlobPtr blob;
} StatItem;

typedef struct {
    i64 link_counter;
    TreeNodePtr left, right;
    i64 stats_count;
    StatItem* stats;
    i64 _[3];
} TreeNode;

// stat allocator
StatBlobPtr stat_blob_allocate(StatTypeId id, i64 size, i64 alignment);
void stat_blob_free(StatTypeId id, StatBlobPtr ptr, i64 size);
void *stat_blob_base_address(StatTypeId id);

// node allocator
// TODO: may be make it depent from OperationId?
TreeNodePtr tree_node_allocate(i64 size, i64 alignment);
void tree_node_free(TreeNodePtr ptr, i64 size);
void *tree_node_base_address();

#endif // CORE_TREE_H
