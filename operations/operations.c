#include "operations.h"

#include <api.h>
#include <ds.h>
#include <mp.h>
#include <network.h>
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

i32 get_operation_no_load(OperationId op_id, Operation *out) {
    rwl_read(&ctx.op_lock);
    i64 pos = hmgeti(ctx.operations, op_id);
    if (pos != -1) {
        *out = ctx.operations[pos].value;
    }
    rwl_unread(&ctx.op_lock);
    return pos != -1 ? GET_OPERATION_SUCCESS : GET_OPERATION_RETRY;
}

i32 get_operation(OperationId op_id, Operation *out) {
    i32 res = get_operation_no_load(op_id, out);
    if (res == GET_OPERATION_RETRY) {
        request_operation(op_id);
        return GET_OPERATION_RETRY;
    }
    return res;
}

i32 fiber_get_operation(OperationId op_id, Operation *out) {
    // TODO: make some way to hook on network getting for operation
    i32 res = get_operation(op_id, out);
    Time start = get_time();
    while (res == GET_OPERATION_RETRY) {
        // TODO: fix wait?
        // wait(NULL, 0);
        mp_sleep(T_MS(100));
        res = get_operation(op_id, out);
        if ((get_time() - start) > T_SEC(3)) {
            return GET_OPERATION_ERROR;
        }
    }
    return res;
}

static OperationId allocate_operation_id() {
    return ctx.op_id_prefix | atomic_fetch_add_explicit(&ctx.next_operation_id, 1, memory_order_relaxed);
}

OperationId create_operation(OperationTypeId type, void *args, u64 args_size) {
    const IOperation *iop = get_operation_type(type);
    assert(iop);

    OperationId new_id = allocate_operation_id();

    Operation op;
    iop->create(&op, args, args_size);

    rwl_write(&ctx.op_lock);

    hmput(ctx.operations, new_id, op);

    rwl_unwrite(&ctx.op_lock);

    return new_id;
}

void deserialize_operation(OperationTypeId type, OperationId op_id, byte *data, u64 size) {
    rwl_read(&ctx.op_lock);
    i64 res = hmgeti(ctx.operations, op_id);
    rwl_unread(&ctx.op_lock);
    if (res != -1) {
        return;
    }

    const IOperation *iop = get_operation_type(type);
    assert(iop);

    Operation op;
    iop->deserialize(&op, data, size);

    rwl_write(&ctx.op_lock);

    res = hmgeti(ctx.operations, op_id);
    if (res == -1) {
        hmput(ctx.operations, op_id, op);
    }

    rwl_unwrite(&ctx.op_lock);

    if (res != -1) {
        iop->destroy(op.self);
    }
}

void init_operation_subsystem(u64 op_id_prefix, u64 next_op_id) {
    assert(op_id_prefix != 0 || next_op_id != 0); // 0 = InvalidOperation
    assert(((op_id_prefix << OPERATION_PREFIX_SHIFT) >> OPERATION_PREFIX_SHIFT) == op_id_prefix);

    rwl_init(&ctx.op_lock);

    ctx.op_id_prefix = op_id_prefix << OPERATION_PREFIX_SHIFT;
    atomic_store_explicit(&ctx.next_operation_id, next_op_id, memory_order_relaxed);
}

void add_operation_type(OperationTypeId id, IOperation *iop) {
    assert(iop->create);
    assert(iop->resolve_anchor);
    assert(iop->decompose);
    assert(iop->unref);
    assert(iop->get_deps);
    assert(iop->serialize);
    assert(iop->deserialize);
    assert(iop->create);
    assert(iop->destroy);
    hmput(ctx.types, id, iop);
}
