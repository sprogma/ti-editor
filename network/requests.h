#ifndef REQUESTS_H
#define REQUESTS_H


#include <api.h>

#define REQUEST(X) typedef struct X X; struct __attribute__((packed, __may_alias__)) X
#define CONTROL_STREAM
#define ANSWER_MASK (0x80000000u)

enum {
    GET_OPERATION_BY_ID,
};

REQUEST (RequestHeader) {
    u32 request_type;
};

STRUCT (Guid) {
    byte value[16];
};

// GetOperationById
CONTROL_STREAM REQUEST (RequestGetOperationById) {
    RequestHeader;
    Guid guid;
    
    OperationId op_id;
};
REQUEST (AnswerGetOperationById) {
    RequestHeader;
    Guid guid;
    
    OperationTypeId tid;
    OperationId op_id;
    u64 data_size;
    byte data[];
};

#endif // REQUESTS_H
