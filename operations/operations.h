#ifndef OPERATIONS_H
#define OPERATIONS_H

#include <types.h>

void init_operation_subsystem(u64 op_id_prefix, u64 next_op_id);

// must be called before creation of any operations
void add_operation_type(OperationTypeId id, IOperation *iop);

#endif // OPERATIONS_H
