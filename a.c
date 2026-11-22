#include "stdio.h"
#include <bestassert.h>

#include <uv.h>
#include <api.h>

int main() {
    // init subsystems
    CoreConfig *cfg = config_start();
    config_add_str(cfg, "module.path", "./test/native/native.dll");
    init_systems(cfg);

    // OperationId res = create_operation(1, NULL, 0);
    // printf("OperationId = %d\n", res);
    // assert(res != InvalidOperation);

    return 0;
}
