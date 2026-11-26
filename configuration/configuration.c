#include "config_impl.h"

#include <bestassert.h>
#include <mp.h>
#include <api.h>
#include <operations.h>
#include <network.h>
#include <scheduler.h>
#include <random.h>
#include <loader.h>


i64 init_systems(CoreConfig *cfg) {
    // simple init
    init_mp_subsystem();
    
    init_task_subsystem();
    
    u64 prefix;
    random_bytes(&prefix, sizeof(prefix));
    init_operation_subsystem((prefix << OPERATION_PREFIX_SHIFT) >> OPERATION_PREFIX_SHIFT, 0);
    
    init_network_subsystem(cfg);

    // load all modules
    const char **modules = get_str(cfg, "module.path");
    for (int i = 0; i < arrlen(modules); ++i) {
        printf("loading module from <%s>\n", modules[i]);
        if (load_module(modules[i]) != 0) {
            printf("got error while loading <%s>\n", modules[i]);
        }
    }

    // populate thread pools
    for (int i = 0; i < 6; ++i) {
        thread_t tid;
        i32 res = thread_create(&tid, worker_thread, NULL);
        assert(res == 0);
    }

    if (arrlen(get_int(cfg, "network.run"))) {
        printf("Running network...");
        start_network();
    }

    config_free(cfg);
    
    return 0;
}
