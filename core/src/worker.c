#include "worker.h"

int worker(void) {
    while (true) {
        with_global_lock() {
            int max_priority = -1, selected = -1;
            for (int i = 0; i < selections_count; ++i) {
                if (selections[i].priority > max_priority) {
                    selections[i].priority = max_priority;
                    selected = i;
                }
            }
            lock_selection(selections[selected].id);
        }

        auto selector = selectors[selections[selected].selector];
        selector(selections[selected].operation, selections[selected].params, selections[selected].params_size);
    }
    return 0;
}

