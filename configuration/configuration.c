#include "config_impl.h"

#include <bestassert.h>
#include <uv.h>
#include <api.h>
#include <operations.h>
#include <scheduler.h>
#include <loader.h>


i64 init_systems(CoreConfig *cfg) {
    // simple init
    init_task_subsystem();
    init_operation_subsystem((0x1 << 32), 0);

    // load all module
    const char **modules = get_str(cfg, "module.path");
    for (int i = 0; i < arrlen(modules); ++i) {
        printf("loading module from <%s>\n", modules[i]);
        if (load_module(modules[i]) != 0) {
            printf("got error while loading <%s>\n", modules[i]);
        }
    }

    // fill operation & statistic types
    IOperation my_op = {
        // TODO: ...
    };
    // add_operation_type(1, &my_op);

    // populate thread pools
    for (int i = 0; i < 6; ++i) {
        uv_thread_t tid;
        assert(uv_thread_create(&tid, worker_thread, NULL) == 0);
    }

    config_free(cfg);
    return 0;
}
