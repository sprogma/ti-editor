#include <api.h>
#include <ds.h>
#include <mp.h>
#include <bestassert.h>

STRUCT (TypeRegistryEntry) {
    OperationTypeId key;
    const IOperation *value;
};

STRUCT (OperationEntry) {
    OperationId key;
    Operation value;
};

static struct {
    TypeRegistryEntry *types;

    u64 op_id_prefix;
    _Atomic u64 next_operation_id;

    rwlock op_lock;
    OperationEntry *operations;
} ctx;

const IOperation *get_operation_type(OperationTypeId type) {
    ptrdiff_t temp;
    return hmget_ts(ctx.types, type, temp);
}

bool get_operation(OperationId op_id, Operation *out) {
    rwl_read(&ctx.op_lock);
    i64 pos = hmgeti(ctx.operations, op_id);
    if (pos != -1) {
        *out = ctx.operations[pos].value;
    }
    rwl_unread(&ctx.op_lock);
    return pos != -1;
}

static OperationId allocate_operation_id() {
    return ctx.op_id_prefix | atomic_fetch_add_explicit(&ctx.next_operation_id, 1, memory_order_relaxed);
}

OperationId create_operation(OperationTypeId type, void *args, u64 args_size) {
    const IOperation *iop = get_operation_type(type);
    if (iop == NULL) return InvalidOperation;

    OperationId new_id = allocate_operation_id();

    rwl_write(&ctx.op_lock);

    hmput(ctx.operations, new_id, (Operation){});
    i64 pos = hmlen(ctx.operations) - 1;
    
    assert(pos == hmgeti(ctx.operations, new_id)); // is must be true always
    
    iop->create(&ctx.operations[pos].value, args, args_size);
    
    rwl_unwrite(&ctx.op_lock);

    return new_id;
}

void init_operation_subsystem(u64 op_id_prefix, u64 next_op_id) {
    assert(op_id_prefix != 0 || next_op_id != 0); // 0 = InvalidOperation
    rwl_init(&ctx.op_lock);

    ctx.op_id_prefix = op_id_prefix;
    atomic_store_explicit(&ctx.next_operation_id, next_op_id, memory_order_relaxed);
}
