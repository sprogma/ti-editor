#ifndef API_H
#define API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// methods marked with this could be nullptr
#define NOT_REQUIRED
// this means this method could be runned ONLY FROM FIBERS
#define FIBER_API
// used in code to mark external symbols
#define API_METHOD

#define STRUCT(X) typedef struct X X; struct X

// ---- value types ----------------------------------------------------------------

#include <types.h>

typedef u64 AnchorId;
typedef u64 StatisticId;
typedef u64 OperationId;
typedef u64 OperationTypeId;

#define InvalidStatistic ((StatisticId)0)
#define InvalidOperation ((OperationId)0)
#define InvalidOperationType ((OperationTypeId)0)

STRUCT (Position) {
    AnchorId anchor;
    i64 offset;
};

STRUCT (Range) {
    Position l, r;
};

STRUCT (ReferenceRange) {
    OperationId op;
    struct Range range;
};

STRUCT (Segment) {
    byte *data;
    u64 count;
};

STRUCT (ReferenceSegment) {
    OperationId op;
    Segment segment;
};

typedef void Stat;

STRUCT (RangeTask) {
    union {
        ReferenceRange operation;
        struct {
            OperationId op; // will be InvalidOperation
            Segment seg;
        };
    };
};
#define RangeTaskIsSegment(t) ((t).op == InvalidOperation)

typedef void DecomposeState;

// ---- interfaces & objects ----------------------------------------------------------------

STRUCT (Operation) {
    OperationTypeId type;
    void *self;
};

STRUCT (IOperation) {
    // TODO: make it batch / make it iterator / fix it in any other way
    // calculates signed distance between anchors. don't requires any unrefs.
    FIBER_API i64 (*resolve_anchor)(void *self, AnchorId base, AnchorId request);
    // decomposes task, also creating stats, and increasing their refcount by one.
    FIBER_API DecomposeState *(*decompose)(void *self, const Range *r);
    // TODO: make next nonblocking
    // count of items. return 0 means this is end, state is freed inside last step.
    FIBER_API u64 (*next)(DecomposeState *state, void *items, u64 max_items);
    // use to free Segment *immediate results from 'decompose'
    void (*unref)(void *self, Segment *segments, u64 count);
    // used to visualizate graph
    u64 (*get_deps)(void *self, OperationId *parents, u64 max_count);
    // to bytes
    u64 (*serialize)(void *self, byte *out, u64 max_size);
    // from bytes [to self]
    void (*deserialize)(Operation* out, byte *data, u64 size);
    // from args
    void (*create)(Operation* out, void *args, u64 args_size);
    // to delete operation
    void (*destroy)(void *self);
    // TODO: add virtual decompose, which will return only ReferenceRange, without any refcount increase
    // NOT_REQUIRED Iterator(ReferenceRange) (*vdecompose)(const Range *r);

    void *global_state;
};

STRUCT (IStatistic) {
    // adds ref
    FIBER_API Stat *(*calc)(void *args, u64 args_size);
    // unref
    void (*unref)(void *args, u64 args_size, Stat *stats, u64 count);
    // TODO: make here 4 arguments instead of 5
    // [for Stat *] to bytes
    u64 (*serialize)(void *args, u64 args_size, Stat *stat, byte *out, u64 max_size);
    // [for Stat *] from bytes
    Stat *(*deserialize)(void *args, u64 args_size, byte *data, u64 size);

    void *global_state;
};

// ---- common api ----------------------------------------------------------------

const IOperation *get_operation_type(OperationTypeId type);
const IStatistic *get_statistic_type(StatisticId type);

bool get_operation(OperationId op_id, Operation *out);
OperationId create_operation(OperationTypeId type, void *args, u64 args_size);

// ---- requests ----------------------------------------------------------------

typedef u64 TaskId;
#define InvalidTaskId 0xFFFFFFFFFFFFFFFFull


STRUCT (RequestAnchor) {
    AnchorId base;
    AnchorId request;
};

STRUCT (RequestStat) {
    StatisticId type;
    u64 args_size;
    void *args;
};

typedef enum {
    RequestInfoType_Data,
    RequestInfoType_Anchor,
    RequestInfoType_Stat,
} RequestInfoType;

STRUCT (RequestInfo) {
    RequestInfoType request_type;
    union {
        ReferenceRange data;
        RequestAnchor anchor;
        RequestStat stat;
    };
};


STRUCT (DataTaskResult) {
    ReferenceSegment *data;
    i64 data_count;
};

// is ready returns 1 if task was really finished. owerwice res == NULL/0
// signed distance
i64 task_get_anchor(TaskId id, i32 *is_ready); 
// result of calc
Stat *task_get_stat(TaskId id, i32 *is_ready);
// get data. op_id can be InvalidOperation in some cases - but that is not error.
DataTaskResult *task_get_data(TaskId id, i32 *is_ready);
// safely frees remote and local segment from task_get_data
void task_unref_segment(DataTaskResult *result);


// ---- networking ----------------------------------------------------------------

typedef u64 NetworkNode;

STRUCT (NetworkStat) {
    // if field is "< 0" that means no data available
    double idle_power; // in ~cpu cores
    double ping; // in seconds
    double bandwidth; // in bytes per second
    u64 free_ram; // in bytes
    u64 free_disk; // in bytes
};

NetworkNode network_get_self();
u64 network_list(NetworkNode *nodes, u64 max_count);
TaskId network_run(NetworkNode node, const RequestInfo *info);
void network_stop(NetworkNode node, TaskId);
void network_stat(NetworkStat *stat, NetworkNode node);

// ---- fiber api ----------------------------------------------------------------

// non zero exit code means THIS task is cancelled, and must return immediatly after releasing resources [and childs]
// to make yield, use wait(nullptr, 0)
FIBER_API u64 wait(TaskId *ids, u64 count);


#ifdef __cplusplus
}
#endif

#endif // API_H
