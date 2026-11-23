#include "statistics.h"

#include <api.h>
#include <ds.h>
#include <mp.h>
#include <bestassert.h>

STRUCT (TypeRegistryEntry) {
    StatisticId key;
    const IStatistic *value;
};

static struct {
    TypeRegistryEntry *types;
} ctx;

const IStatistic *get_statistic_type(StatisticId type) {
    ptrdiff_t temp;
    return hmget_ts(ctx.types, type, temp);
}

void init_statistic_subsystem() {
}

void add_statistic_type(StatisticId id, IStatistic *ist) {
    assert(ist->calc);
    assert(ist->unref);
    assert(ist->serialize);
    assert(ist->deserialize);
    hmput(ctx.types, id, ist);
}
