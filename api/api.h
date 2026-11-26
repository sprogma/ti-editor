#ifndef API_H
#define API_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
    #ifdef CORE_BUILD
        #define CORE_API __declspec(dllexport)
    #else
        #define CORE_API __declspec(dllimport)
    #endif
#else
    #ifdef CORE_BUILD
        #define CORE_API __attribute__((visibility("default")))
    #else
        #define CORE_API
    #endif
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
typedef u64 ModuleId;

#define InvalidStatistic ((StatisticId)0)
#define InvalidOperation ((OperationId)0)
#define InvalidOperationType ((OperationTypeId)0)

#define OPERATION_PREFIX_SHIFT 40
#define OPERATION_PREFIX_MASK (-(1 << OPERATION_PREFIX_SHIFT))

STRUCT (Position) {
    AnchorId anchor;
    i64 offset;
};

STRUCT (Range) {
    Position l, r;
};

STRUCT (ReferenceRange) {
    OperationId op;
    Range range;
};

STRUCT (Segment) {
    const byte *data;
    u64 size;
};

STRUCT (ReferenceSegment) {
    OperationId op;
    Segment seg;
};

typedef void Stat;

STRUCT (RangeTask) {
    union {
        ReferenceRange ref;
        // TODO: may be use ReferenceSegment here
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
    ModuleId module;
    // TODO: make it batch / make it iterator / fix it in any other way
    // calculates signed distance between anchors. don't requires any unrefs.
    FIBER_API i64 (*resolve_anchor)(void *self, AnchorId base, AnchorId request);
    // TODO: convert decompose to iterator
    // decomposes task, also creating stats, and increasing their refcount by one.
    FIBER_API u64 (*decompose)(void *self, const Range *r, RangeTask *items, u64 max_items);
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
    // to free operation
    void (*destroy)(void *self);
    // TODO: add virtual decompose, which will return only ReferenceRange, without any refcount increase
    // NOT_REQUIRED Iterator(ReferenceRange) (*vdecompose)(const Range *r);

    void *global_state;
};

STRUCT (IStatistic) {
    ModuleId module;
    // adds ref
    FIBER_API Stat *(*calc)(void *args, u64 args_size);
    // TODO: may be remove void *args, u64 args_size from this function...
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

CORE_API const IOperation *get_operation_type(OperationTypeId type);
CORE_API const IStatistic *get_statistic_type(StatisticId type);

// must be called ONLY IN 'load_module'
CORE_API void add_operation_type(OperationTypeId id, IOperation *iop);
CORE_API void add_statistic_type(StatisticId id, IStatistic *ist);

#define GET_OPERATION_SUCCESS 0
#define GET_OPERATION_RETRY 1

CORE_API i32 get_operation(OperationId op_id, Operation *out);
CORE_API i32 get_operation_no_load(OperationId op_id, Operation *out);
CORE_API FIBER_API i32 fiber_get_operation(OperationId op_id, Operation *out); // blocking, !canNOT! return GET_OPERATION_RETRY
CORE_API OperationId create_operation(OperationTypeId type, void *args, u64 args_size);
CORE_API void deserialize_operation(OperationTypeId type, OperationId op_id, byte *data, u64 size);

// ---- requests ----------------------------------------------------------------

typedef u64 TaskId;
#define InvalidTaskId 0xFFFFFFFFFFFFFFFFull


STRUCT (RequestAnchor) {
    OperationId op;
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
CORE_API i64 task_get_anchor(TaskId id, i32 *is_ready); 
// result of calc
CORE_API Stat *task_get_stat(TaskId id, i32 *is_ready);
// get data. op_id can be InvalidOperation in some cases - but that is not error.
CORE_API DataTaskResult *task_get_data(TaskId id, i32 *is_ready);
// safely frees remote and local segment from task_get_data
CORE_API void task_unref_segment(DataTaskResult *result);


// ---- networking ----------------------------------------------------------------

typedef u64 NetworkNode;

STRUCT (NetworkStat) {
    // if field is "< 0" that means no data available
    double idle_power; // in ~cpu cores
    Time ping;
    u64 bandwidth; // in BYTES per second [!]
    u64 free_ram; // in bytes
    u64 free_disk; // in bytes
};

CORE_API NetworkNode network_get_self();
CORE_API u64 network_list(NetworkNode *nodes, u64 max_count);
CORE_API void network_stat(NetworkStat *stat, NetworkNode node);
CORE_API void network_run(NetworkNode node, TaskId *dest, const RequestInfo *info, u64 info_count);
CORE_API void network_stop(TaskId id, u64 cancel_code);

// ---- fiber api ----------------------------------------------------------------

// non zero exit code means THIS task is cancelled, and must return immediatly after releasing resources [and childs]
// to make yield, use wait(nullptr, 0)
CORE_API FIBER_API u64 wait(TaskId *ids, u64 count);


// ---- modules ----------------------------------------------------------------

STRUCT (Module) {
    ModuleId id;
    const char *name;
    const char *loaded_path;
    u64 version;
    // TODO: add some query interface
};


const Module *get_module(ModuleId id);


// ---- configuration ----------------------------------------------------------------
typedef struct CoreConfig CoreConfig;

CORE_API CoreConfig *config_start(void);
CORE_API void config_add_int(CoreConfig *cfg, const char *key, i64 val);
CORE_API void config_add_float(CoreConfig *cfg, const char *key, double val);
CORE_API void config_add_str(CoreConfig *cfg, const char *key, const char *val);

CORE_API i64 init_systems(CoreConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif // API_H
