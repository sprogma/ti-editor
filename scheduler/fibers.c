#include "fibers_impl.h"

#include <mp.h>
#include <bestassert.h>

static void internal_fiber_init(FiberInfo *f, void *stack_mem, size_t stack_size, TaskFunction fn, void *arg) {
    uintptr_t top = ((uintptr_t)stack_mem + stack_size) & ~0xFULL;
    u64 *sp = (u64 *)top;

#ifdef _WIN32
    sp -= 4;                   // shadow space
#endif
    *(--sp) = (u64)internal_fiber_on_return_jump; // ret for fn()
    *(--sp) = (u64)internal_fiber_start;     // ret for fiber_resume()

#ifdef _WIN32
    *(--sp) = 0;               // rbp
    *(--sp) = 0;               // rbx
    *(--sp) = 0;               // rdi
    *(--sp) = 0;               // rsi
    *(--sp) = (u64)fn;         // r12
    *(--sp) = (u64)arg;        // r13
    *(--sp) = 0;               // r14
    *(--sp) = 0;               // r15
    *(--sp) = top;             // TEB StackBase
    *(--sp) = (u64)stack_mem;  // TEB StackLimit
    sp -= 20;                  // xmm6-xmm15
#else
    *(--sp) = 0;               // rbp
    *(--sp) = 0;               // rbx
    *(--sp) = (u64)fn;         // r12
    *(--sp) = (u64)arg;        // r13
    *(--sp) = 0;               // r14
    *(--sp) = 0;               // r15
#endif

    f->rsp = sp;
}


// ---- external methods ---------------------------------

// stack_mem = NULLPTR = allocate dynamic stack
i32 fiber_init(FiberInfo *f, void *stack_mem, size_t stack_size, TaskFunction fn, void *arg) {

    if (stack_mem == NULL) {
        stack_size = 64 * STACK_PAGE_SIZE;
#ifdef _WIN32
        stack_mem = VirtualAlloc(NULL, stack_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        stack_mem = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (stack_mem == MAP_FAILED) {
            return 1;
        }
#endif
    }

    if (stack_mem == NULL) {
        return 1;
    }

    internal_fiber_init(f, stack_mem, stack_size, fn, arg);

    return 0;
}

void fiber_cancel(FiberInfo *fib, u64 cancel_code) {
    fib->pending_cancellation = cancel_code;
}
