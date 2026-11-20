#include "stdio.h"
#include <bestassert.h>

#include <api.h>
#include <operations.h>
#include <scheduler.h>

int main() {
    init_task_subsystem();
    init_operation_subsystem((0x1 << 32), 0);

    IOperation my_op = {
        // TODO: ...
    };

    add_operation_type(1, &my_op);

    OperationId res = create_operation(1, NULL, 0);

    printf("OperationId = %d\n", res);
    assert(res != InvalidOperation);
    
    return 0;
}
