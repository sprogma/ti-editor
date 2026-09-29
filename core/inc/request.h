#ifndef CORE_REQUEST_H
#define CORE_REQUEST_H

#include "types.h"

#include "data.h"
#include "tree.h"
#include "operation.h"

typedef enum {
    QueueKind_Chunk;
    QueueKind_Stat;
} QueueKind;

typedef i64 QueueId;
typedef i64 SelectorId;
typedef i64 SelectorTypeId;

typedef struct {
    ......
} SelectorType;

// api
QueueId start_request(OperationId operation, SelectorId selector, void *params, i64 priority);
i64 get_completed_queues(/*out*/ QueueId* queues, i64 max_count); // return queue if there is any data (not only if it is fully completed)
// only for QueueKind_Chunk
....How to tell that queue is end?
i64 get_completed_chunks(QueueId queue, /*out*/ Chunk* chunks, i64 max_count);
void release_completed_chunks(Chunk *chunks, i64 count);
// only for QueueKind_Stat
i64 get_completed_stats(QueueId queue, /*out*/ StatResult* chunks, i64 max_count); // commonly return 1 stat result, but some selectors may want to give "real-time changing" answers



#endif
