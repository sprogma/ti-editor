#ifndef CORE_API_REQUEST
#define CORE_API_REQUEST

#include "types.h"

typedef enum : i64 {
    RequestType_Incomplete = -1,
    RequestType_IgnoreError = -2,
    RequestType_IgnoreRepeat = -3,
    
    
    // send: node don't know path to chunk, set bad_distance to INT_MAX
    // send: neigbour wich was used for this chunk is broken, so need another route
    // send: node knew path, but received Get_Error request and need another route
    RequestType_PathChunk_GetError = 0x1000,
    // send: answer to _Get or _GetError request
    RequestType_PathChunk_Answer = 0x1001,

    
} RequestType;


#endif // CORE_API_REQUEST
