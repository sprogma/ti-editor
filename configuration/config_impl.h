#ifndef CONFIG_IMPL_H
#define CONFIG_IMPL_H

#include <ds.h>
#include <types.h>
#include <api.h>

typedef struct CoreConfig CoreConfig;

void config_free(CoreConfig *cfg);

i64 *get_int(CoreConfig *cfg, const char *key);
double *get_float(CoreConfig *cfg, const char *key);
const char **get_str(CoreConfig *cfg, const char *key);

i64 get_one_int(CoreConfig *cfg, const char *key, i64 def);
double get_one_float(CoreConfig *cfg, const char *key, double def);
const char *get_one_str(CoreConfig *cfg, const char *key, const char *def);

#endif // CONFIG_IMPL_H
