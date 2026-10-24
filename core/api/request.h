#ifndef CORE_API_REQUEST
#define CORE_API_REQUEST

#include "types.h"

typedef enum : i64 {
    RequestType_Incomplete = -1,
    RequestType_IgnoreError = -2,
    RequestType_IgnoreRepeat = -3,
    
    RequestType_PathChunk = 1,
    RequestType_PathStat = 2,

    
} RequestType;


#endif // CORE_API_REQUEST
