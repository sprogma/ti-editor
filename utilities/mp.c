#include "mp.h"

#include <limits.h>
#include <bestassert.h>

#ifdef _WIN32

#include <process.h>

int thread_create(thread_t *t, thread_func_t func, void *arg) {
    uintptr_t h = _beginthreadex(NULL, 0, (unsigned (__stdcall *)(void *))(void *)func, arg, 0, NULL);
    if (h == 0) return -1;
    *t = (HANDLE)h;
    return 0;
}

void thread_join(thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

void mp_sleep(Time t) {
    u64 ms = t / 1000000;
    assert(ms < UINT32_MAX);
    Sleep(ms);
}

Time freq = 0;

Time get_time(void) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    Time ticks = (Time)counter.QuadPart;

    if (__builtin_expect(freq == 10000000ULL, 1)) {
        return ticks * 100ULL;
    }

    u64 whole = (ticks / freq) * 1000000000ULL;
    u64 part  = (ticks % freq) * 1000000000ULL / freq;
    return whole + part;
}

void init_mp_subsystem(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = (Time)f.QuadPart;
}

#else

#include <time.h>

int thread_create(thread_t *t, thread_func_t func, void *arg) {
    return pthread_create(t, NULL, (void *(*)(void *))(void *)func, arg);
}

void thread_join(thread_t t) {
    pthread_join(t, NULL);
}

void mp_sleep(u64 t) {
    struct timespec req, rem;
    req.tv_sec  = t / 1000000000;
    req.tv_nsec = t % 1000000000;
    while (nanosleep(&req, &rem) == -1) {
        if (errno != EINTR) {
            break;
        }
        req = rem;
    }
}

Time gget_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (Time)ts.tv_sec * 1000000000ULL + (Time)ts.tv_nsec;
}

void init_mp_subsystem(void) {
}

#endif
