#ifndef INC_MP_H
#define INC_MP_H

#include <stdatomic.h>
#include <types.h>

#if defined(_WIN32) || defined(_WIN64)
    #define NOGDI
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    typedef SRWLOCK rwlock;
    #define RWLOCK_INIT SRWLOCK_INIT
    #define rwl_init(l) InitializeSRWLock(l)
    #define rwl_read(l) AcquireSRWLockShared(l)
    #define rwl_write(l) AcquireSRWLockExclusive(l)
    #define rwl_unread(l) ReleaseSRWLockShared(l)
    #define rwl_unwrite(l) ReleaseSRWLockExclusive(l)
    #define rwl_destroy(l) /* none */

    typedef HANDLE thread_t;
#else
    #include <pthread.h>
    typedef pthread_rwlock_t rwlock;
    #define RWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
    #define rwl_init(l) pthread_rwlock_init(l, NULL)
    #define rwl_read(l) pthread_rwlock_rdlock(l)
    #define rwl_write(l) pthread_rwlock_wrlock(l)
    #define rwl_unread(l) pthread_rwlock_unlock(l)
    #define rwl_unwrite(l) pthread_rwlock_unlock(l)
    #define rwl_destroy(l) pthread_rwlock_destroy(l)

    typedef pthread_t thread_t;
#endif

typedef void (*thread_func_t)(void *);

int thread_create(thread_t *t, thread_func_t func, void *arg);
void thread_join(thread_t t);

Time get_time(void);
void mp_sleep(Time t);

void init_mp_subsystem(void);

#endif // INC_MP_H
