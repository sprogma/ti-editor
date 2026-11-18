#ifndef INC_MP_H
#define INC_MP_H

#if defined(_WIN32) || defined(_WIN64)
    #define NOMINMAX
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
#endif

#endif // INC_MP_H
