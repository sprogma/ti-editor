#include <api.h>

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#ifdef _WIN32
    #define API __declspec(dllexport)
#else
    #define API
#endif

#define AnchorBegin 1
#define AnchorEnd 2
#define AbobaOpType 0xAB0BA
#define TextOpType 179

typedef void AbobaOp;

i64 aboba_resolve_anchor(void *ptr_data, AnchorId base, AnchorId request) {
    AbobaOp *self = ptr_data;
    if (base == request) {
        return 0;
    } else {
        return base == AnchorBegin ? 5 : -5;
    }
}

i64 aboba_get_pos(Position pos) {
    if (pos.anchor == AnchorBegin) {
        return pos.offset;
    }
    if (pos.anchor == AnchorEnd) {
        return 5 + pos.offset;
    }
    return -1;
}

u64 aboba_decompose(void *ptr_data, const Range *range, RangeTask *items, u64 max_items) {
    AbobaOp *self = ptr_data;
    assert(max_items > 0);
    i64 l = aboba_get_pos(range->l), r = aboba_get_pos(range->r);
    if (l >= r || l < 0 || r < 0) {
        return 0;
    }
    items[0].op = InvalidOperation;
    items[0].seg.data = ((const byte *)"aboba") + l;
    items[0].seg.size = r - l;
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

void aboba_deserialize(Operation *out, byte *data, u64 size) {
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
    .unref = aboba_unref,
    .get_deps = aboba_get_deps,
    .serialize = aboba_serialize,
    .deserialize = aboba_deserialize,
    .create = aboba_create,
    .destroy = aboba_destory,
    .global_state = NULL,
};









typedef void TextOp;

i64 text_resolve_anchor(void *ptr_data, AnchorId base, AnchorId request) {
    TextOp *self = ptr_data;
    if (base == request) {
        return 0;
    } else {
        return base == AnchorBegin ? strlen(self) : -strlen(self);
    }
}

i64 text_get_pos(TextOp *self, Position pos) {
    if (pos.anchor == AnchorBegin) {
        return pos.offset;
    }
    if (pos.anchor == AnchorEnd) {
        return strlen(self) + pos.offset;
    }
    return -1;
}

u64 text_decompose(void *ptr_data, const Range *range, RangeTask *items, u64 max_items) {
    TextOp *self = ptr_data;
    assert(max_items > 0);
    i64 l = text_get_pos(self, range->l), r = text_get_pos(self, range->r);
    if (l >= r || l < 0 || r < 0) {
        return 0;
    }
    items[0].op = InvalidOperation;
    items[0].seg.data = ((const byte *)(self)) + l;
    items[0].seg.size = r - l;
    return 1;
}

void text_unref(void *ptr_data, Segment *segments, u64 count) {
    TextOp *self = ptr_data;
    (void)self;
}

u64 text_get_deps(void *ptr_data, OperationId *parents, u64 max_count) {
    TextOp *self = ptr_data;
    (void)self;
    return 0;
}

u64 text_serialize(void *ptr_data, byte *out, u64 max_size) {
    TextOp *self = ptr_data;
    memcpy(out, self, strlen(self));
    return strlen(self);
}

void text_deserialize(Operation *out, byte *data, u64 size) {
    out->type = TextOpType;
    out->self = malloc(size + 1);
    memcpy(out->self, data, size);
    ((byte *)out->self)[size] = 0;
}

void text_create(Operation *out, void *args, u64 args_size) {
    out->type = TextOpType;
    out->self = malloc(args_size + 1);
    memcpy(out->self, args, args_size);
    ((byte *)out->self)[args_size] = 0;
}

void text_destory(void *ptr_data) {
    TextOp *self = ptr_data;
    free(self);
}

IOperation text_operation = {
    .resolve_anchor = text_resolve_anchor,
    .decompose = text_decompose,
    .unref = text_unref,
    .get_deps = text_get_deps,
    .serialize = text_serialize,
    .deserialize = text_deserialize,
    .create = text_create,
    .destroy = text_destory,
    .global_state = NULL,
};




API i64 load_module(Module *info) {
    info->id = 0xAB0BA;
    info->name = "aboba module I";
    info->version = 0;

    add_operation_type(AbobaOpType, &aboba_operation);
    add_operation_type(TextOpType, &text_operation);
    return 0;
}
