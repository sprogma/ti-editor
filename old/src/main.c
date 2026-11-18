#include <server.h>
#include <uv.h>

#include <stdio.h>

typedef struct {
    int argc;
    char **argv;
    uv_loop_t *loop;
} server_worker_data;

void server_worker(server_worker_data* data) {
    server_run(data->argc, data->argv, &data->loop);
}

int main(int argc, char** argv) {
    init_types();

    int res;
    uv_thread_t server_id;
    server_worker_data data = {.argc = argc, .argv = argv};
    res = uv_thread_create(&server_id, server_worker, &data);
    if (res) {
        fprintf(stderr, "Server worker start failed: %s", uv_strerror(res));
    }

    while (true) {
        printf("> ");
        char cmd[128];
        scanf("%s", cmd);
        if (strcmp(cmd, "exit") == 0) {
            break;
        }
    }

    while (!handle->loop) {

    }
    
    uv_stop(handle->loop);
    uv_thread_join(&thread_id);

    return server_run(argc, argv);
}

