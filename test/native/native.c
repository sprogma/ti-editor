#include <api.h>

#include <assert.h>

#define AbobaOpType 0xAB0BA

i64 aboba_resolve_anchor(void *ptr_data, AnchorId base, AnchorId request) {
    AbobaOp *self = ptr_data;
    if (base == request) {
        return 0;
    } else {
        return base == 0 ? 5 : -5;
    }
}

DecomposeState *aboba_decompose(void *ptr_data, const Range *r) {
    AbobaOp *self = ptr_data;
    DecomposeState *state = malloc(sizeof(*r));
    *(Range *)state = *r;
    return state;
}

u64 aboba_next(void *ptr_data, DecomposeState *state, RangeTask *items, u64 max_items) {
    AbobaOp *self = ptr_data;
    assert(max_items > 0);
    ... [need to return 1 / 0, or change next protocol...]
    items[0].op = InvalidOperation;
    items[0].seg.data = "aboba";
    items[0].seg.size = 5;
    ...
    return 1;
}

void aboba_unref(void *ptr_data, Segment *segments, u64 count) {
    AbobaOp *self = ptr_data;
    (void)self;
}

u64 aboba_get_deps(void *ptr_data, OperationId *parents, u64 max_count) {
    AbobaOp *self = ptr_data;
    (void)self;
    return 0;
}

u64 aboba_serialize(void *ptr_data, byte *out, u64 max_size) {
    AbobaOp *self = ptr_data;
    (void)self;
    return 0;
}

void aboba_deserialize(Operation *out, byte *out, u64 size) {
    out->type = AbobaOpType;
}

void aboba_create(Operation *out, void *args, u64 args_size) {
    out->type = AbobaOpType;
}

void aboba_destory(void *ptr_data) {
    AbobaOp *self = ptr_data;
    (void)self;
}

IOperation aboba_operation = {
    .resolve_anchor = aboba_resolve_anchor,
    .decompose = aboba_decompose,
    .next = aboba_next,
    .unref = aboba_unref,
    .get_deps = aboba_get_deps,
    .serialize = aboba_serialize,
    .deserialize = aboba_deserialize,
    .create = aboba_create,
    .destroy = aboba_destory,
    .global_state = NULL,
};

i64 load_module(Module *info) {
    info->id = 0xAB0BA;
    info->name = "aboba module I";
    info->version = 0;

    add_operation_type(AbobaOpType, &aboba_operation);
}
