#ifndef TYPES_H
#define TYPES_H

typedef int i32;
typedef long long i64;
typedef unsigned int u32;
typedef unsigned short int u16;
typedef unsigned long long u64;
typedef unsigned char byte;

#define T_MIN(x) ((x) * 60000000000ull)
#define T_SEC(x) ((x) * 1000000000ull)
#define T_MS(x) ((x) * 1000000ull)
#define T_US(x) ((x) * 1000ull)
typedef i64 Time; // nanoseconds

#endif
