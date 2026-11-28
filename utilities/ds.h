#ifndef DS_H
#define DS_H

#include <stb_ds.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#define max(a, b) ({            \
    __typeof__(a) _a = (a);     \
    __typeof__(b) _b = (b);     \
    _a > _b ? _a : _b;          \
})

#define min(a, b) ({            \
    __typeof__(a) _a = (a);     \
    __typeof__(b) _b = (b);     \
    _a < _b ? _a : _b;          \
})

#endif // DS_H
