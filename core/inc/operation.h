#ifndef CORE_OPERATION_H
#define CORE_OPERATION_H

#include "types.h"

#include "data.h"
#include "tree.h"

typedef i64 OperationTypeId;
typedef i64 OperationId;

typedef i64 OperationDataPtr;

typedef struct {
    OperationTypeId id;
    const char *name;

    // TODO: may be add void *ctx field... (and parameter to all functions)?
    // void *ctx;

    void (*start)(OperationId op);
    // given chunk must be chunk of op->parent operation.
    // result chunk must be it's result.
    // there must be a chunk cache (maybe using 
    // operation::TreeNodePtr) inside this function.
    ChunkId (*process)(OperationId op, ChunkId cnk);
    void (*free)(OperationId op);

    i64  (*store_size)(const OperationId op);
    // must write exactly store_size(op) bytes
    void (*store_data)(const OperationId op, u8 *dest); 
    // must start the same operation
    // len will be stored by external api, it is exact result of store_size(op)
    bool (*load)(OperationId *op, const u8 *source, i64 len);
} OperationType;

typedef struct {
    OperationTypeId type;
    OperationId parent;
    OperationDataPtr data;
    TreeNodePtr root;
} Operation; 

// operation data
OperationDataPtr op_data_ptr_allocate(OperationTypeId op, i64 size, i64 alignment);
void op_data_ptr_free(OperationTypeId op, OperationDataPtr ptr, i64 size);
Label *op_data_ptr_base_address(OperationTypeId op);

// creation api
void init_engine(void);
OperationId start_operation(OperationId parent, OperationTypeId type, OperationDataPtr data);
Operation get_operation(OperationId op);

#endif // CORE_OPERATION_H
