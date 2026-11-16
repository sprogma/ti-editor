#ifndef TYPES_H
#define TYPES_H

#include "inttypes.h"

typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t byte;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t Time; // nanoseconds

typedef i32 Ptr;

Time get_time();
Time timeout_from_now(Time microseconds);
#define T_SEC(x) (x * 1000000000LL)
#define T_MS(x) (x * 1000000LL)
#define T_US(x) (x * 1000LL)
void init_types();

#endif // TYPES_H

