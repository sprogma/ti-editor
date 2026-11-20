#include "stdio.h"
#include <bestassert.h>

#include <api.h>
#include <operations.h>
#include <scheduler.h>

int main() {
    init_task_subsystem();
    init_operation_subsystem((0x1 << 32), 0);

    int res = printf("Hello\n");

    assert(res == 3);
    return 0;
}
