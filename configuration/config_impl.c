#include "config_impl.h"


STRUCT (ConfigInt) {
    const char *key;
    i64 *value;
};

STRUCT (ConfigFloat) {
    const char *key;
    double *value;
};

STRUCT (ConfigString) {
    const char *key;
    const char **value;
};

struct CoreConfig {
    ConfigString *str;
    ConfigInt *num;
    ConfigFloat *flt;
};

CoreConfig *config_start(void) {
    CoreConfig *cfg = calloc(1, sizeof(*cfg));
    return cfg;
}

void config_free(CoreConfig *cfg) {
    if (!cfg) return;

    for (ptrdiff_t i = 0; i < shlen(cfg->num); ++i) {
        arrfree(cfg->num[i].value);
    }
    shfree(cfg->num);

    for (ptrdiff_t i = 0; i < shlen(cfg->flt); ++i) {
        arrfree(cfg->flt[i].value);
    }
    shfree(cfg->flt);

    for (ptrdiff_t i = 0; i < shlen(cfg->str); ++i) {
        arrfree(cfg->str[i].value);
    }
    shfree(cfg->str);

    free(cfg);
}

void config_add_int(CoreConfig *cfg, const char *key, i64 val) {
    if (!cfg) return;
    ptrdiff_t idx = shgeti(cfg->num, key);
    if (idx == -1) {
        i64 *arr = NULL;
        arrput(arr, val);
        shput(cfg->num, key, arr);
    } else {
        arrput(cfg->num[idx].value, val);
    }
}

void config_add_float(CoreConfig *cfg, const char *key, double val) {
    if (!cfg) return;
    ptrdiff_t idx = shgeti(cfg->flt, key);
    if (idx == -1) {
        double *arr = NULL;
        arrput(arr, val);
        shput(cfg->flt, key, arr);
    } else {
        arrput(cfg->flt[idx].value, val);
    }
}

void config_add_str(CoreConfig *cfg, const char *key, const char *val) {
    if (!cfg) return;
    ptrdiff_t idx = shgeti(cfg->str, key);
    if (idx == -1) {
        const char **arr = NULL;
        arrput(arr, val);
        shput(cfg->str, key, arr);
    } else {
        arrput(cfg->str[idx].value, val);
    }
}

i64 *get_int(CoreConfig *cfg, const char *key) {
    if (!cfg) return NULL;
    ptrdiff_t idx = shgeti(cfg->num, key);
    return (idx != -1) ? cfg->num[idx].value : NULL;
}

double *get_float(CoreConfig *cfg, const char *key) {
    if (!cfg) return NULL;
    ptrdiff_t idx = shgeti(cfg->flt, key);
    return (idx != -1) ? cfg->flt[idx].value : NULL;
}

const char **get_str(CoreConfig *cfg, const char *key) {
    if (!cfg) return NULL;
    ptrdiff_t idx = shgeti(cfg->str, key);
    return (idx != -1) ? cfg->str[idx].value : NULL;
}
