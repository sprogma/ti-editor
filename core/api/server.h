#ifndef CORE_API_SERVER_H
#define CORE_API_SERVER_H

#include <types.h>
#include <uv.h>

typedef struct {
    i32 high_chunk_id;
    i32 low_chunk_id;
} LocalServer;

int server_run(int argc, char **argv, uv_loop_t **ploop);

#endif // CORE_API_SERVER_H
