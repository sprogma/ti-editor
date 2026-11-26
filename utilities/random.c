#include "random.h"
#include <types.h>

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <bcrypt.h>
    #pragma comment(lib, "bcrypt.lib")
#elif defined(__linux__)
    #include <sys/random.h>
#endif

bool random_bytes(void* buffer, size_t size) {
#if defined(_WIN32)
    return BCRYPT_SUCCESS(BCryptGenRandom(NULL, (PUCHAR)(buffer), (ULONG)(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(__linux__)
    size_t total = 0;
    auto* ptr = (byte*)(buffer);
    while (total < size) {
        ssize_t res = getrandom(ptr + total, size - total, 0);
        if (res <= 0) return false;
        total += static_cast<size_t>(res);
    }
    return true;
#else
    return false;
#endif
}

