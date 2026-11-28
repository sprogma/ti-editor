#ifndef REQUESTS_H
#define REQUESTS_H


#include <api.h>

#define REQUEST(X) typedef struct X X; struct __attribute__((packed, __may_alias__)) X
#define CONTROL_STREAM
#define ANSWER_MASK (0x80000000u)

enum {
    GET_OPERATION_BY_ID,
    GET_TASK_RESULT,
    START_TASK,
    SHARE_STATISTIC,
};

REQUEST (RequestHeader) {
    u32 request_type;
};

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

CONTROL_STREAM REQUEST (RequestShareStatistic) {
    RequestHeader;

    PeerStat stat;
};

STRUCT (NetworkRequestStat) {
    StatisticId type;
    u64 args_size;
    u64 args_offset;
};

typedef enum {
    RequestInfoType_Data,
    RequestInfoType_Anchor,
    RequestInfoType_Stat,
} RequestInfoType;

STRUCT (NetworkRequestInfo) {
    RequestInfoType request_type;
    Guid request_guid;
    union {
        ReferenceRange data;
        RequestAnchor anchor;
        NetworkRequestStat stat;
    };
};

CONTROL_STREAM REQUEST (RequestStartTask) { // TODO: move to new stream if size is big
    RequestHeader;
    Guid guid;

    NetworkNode node;
    u32 requests_count;
    NetworkRequestInfo requests[]; // TODO: pack data, instead of union
};

CONTROL_STREAM REQUEST (AnswerStartTask) {
    RequestHeader;
    Guid guid;

    NetworkNode node;
    u32 tasks_count;
    TaskId tasks[];
};

REQUEST (RequestGetTaskResult) {
    RequestHeader;
    Guid guid;

    NetworkNode node;
    TaskId task;
};

REQUEST (AnswerGetTaskResult) {
    RequestHeader;
    Guid guid;

    NetworkNode node;
    TaskId task;
    u32 data_size;
    byte data[];
};

#endif // REQUESTS_H
