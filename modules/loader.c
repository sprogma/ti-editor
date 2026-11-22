#include "loader.h"

#include "stdlib.h"
#include <api.h>
#include <mp.h>
#include <ds.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

typedef i64 (*module_init_fn)(Module *info);

STRUCT (ModuleKey) {
    ModuleId key;
    Module *value;
};

struct {
    rwlock lock;

    ModuleKey *modules;
} ctx;

void init_module_subsystem(void) {
    rwl_init(&ctx.lock);
}

i64 load_module(const char *path) {
    if (!path || !*path) {
        return -1;
    }

    rwl_write(&ctx.lock);
    
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path);
    if (!handle) {
        rwl_unwrite(&ctx.lock);
        return -2;
    }

    module_init_fn init_fn = (module_init_fn)(void *)GetProcAddress(handle, "load_module");
    if (!init_fn) {
        FreeLibrary(handle);
        rwl_unwrite(&ctx.lock);
        return -3;
    }
#else
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        rwl_unwrite(&ctx.lock);
        return -2;
    }

    dlerror();
    module_init_fn init_fn = (module_init_fn)dlsym(handle, "load_module");
    if (!init_fn) {
        dlclose(handle);
        rwl_unwrite(&ctx.lock);
        return -3;
    }
#endif

    Module *info = calloc(sizeof(*info), 1);
    info->loaded_path = path;
    i64 res = init_fn(info);
    if (res < 0) {
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        rwl_unwrite(&ctx.lock);
        return res;
    }

    // restore info
    info->loaded_path = path;

    hmput(ctx.modules, info->id, info);
    
    rwl_unwrite(&ctx.lock);
    return res;
}

const Module *get_module(ModuleId id) {
    rwl_read(&ctx.lock);

    Module *res = NULL;

    i64 pos = hmgeti(ctx.modules, id);
    if (pos != -1) {
        res = &ctx.modules[pos].value;
    }
    
    rwl_unread(&ctx.lock);
    return res;
}
