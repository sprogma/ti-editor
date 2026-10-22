#ifndef TYPES_H
#define TYPES_H

#include "inttypes.h"

typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t byte;
typedef uint64_t u64;

typedef int64_t TimeUs;
typedef int64_t Ticks;

typedef i32 Ptr;

Ticks us_to_ticks(TimeUs us);
TimeUs ticks_to_us(Ticks ticks);
Ticks get_ticks();
Ticks timeout_from_now(TimeUs microseconds);
void init_types();

#endif // TYPES_H

