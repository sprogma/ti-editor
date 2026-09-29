#include "core.h"
#include "master.h"
#include "worker.h"


OperationId start_operation(OperationId parent, OperationType type, i64 data_size, void *data) {
    operations.push_back({type, parent, data_size, data});
}

QueueId start_request(OperationId operation, SelectorId selector, void *params, i64 priority) {
    selections.push_back({operation, selector, params, priority});
}

i64 get_completed_queues(/*out*/ QueueId* queues, i64 max_count) {
    ans;
    for (i : queues) {
        if (i.completed_count) {
            ans.push_back(i);
        }
    }
    return ans;
}

i64 get_completed_chunks(/*out*/ Chunk* chunks, i64 max_count, QueueId queue) {
    memcpy(chunks, quques[queue].completed, sizeof(*chunks) * queues[queue].completed_count);
    return queues[queue].completed_count;
}

void release_completed_chunks(Chunk *chunks, i64 count) {
    for (int i = 0; i < count; ++i) {
        release_chunk(chunks[i].data);
        release_chunk(chunks[i].label);
    }
}

void init_engine(void) {
    start_thread(master);
}
