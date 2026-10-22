#include <types.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if defined(_WIN32) || defined(_WIN64)

static i64 frequency = 0;

i64 us_to_ticks(i64 us)
{
    if (frequency == 0) return 0;
    
    if (frequency == 10000000LL) {
        return us * 10LL;
    } else if (frequency % 1000000LL == 0) {
        return us * (frequency / 1000000LL);
    }
    
    return (i64)((double)us * ((double)frequency / 1000000.0));
}

i64 ticks_to_us(i64 ticks)
{
    if (frequency == 0) return 0;

    if (frequency == 10000000LL) {
        return ticks / 10LL;
    } else if (frequency % 1000000LL == 0) {
        return ticks / (frequency / 1000000LL);
    } else if (1000000LL % frequency == 0) {
        return ticks * (1000000LL / frequency);
    }

    return (i64)((double)ticks * (1000000.0 / (double)frequency));
}

i64 get_ticks()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

i64 timeout_from_now(i64 us)
{
    return get_ticks() + us_to_ticks(us);
}

#else

#include <time.h>

i64 us_to_ticks(i64 us)
{
    return us;
}

i64 ticks_to_us(i64 ticks)
{
    return ticks;
}

i64 get_ticks()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);    
    return (i64)ts.tv_sec * 1000000LL + (i64)ts.tv_nsec / 1000LL;
}

i64 timeout_from_now(i64 us)
{
    return get_ticks() + us_to_ticks(us);
}

#endif

void init_types() {
#if defined(_WIN32) || defined(_WIN64)
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    frequency = freq.QuadPart;
#endif
}

