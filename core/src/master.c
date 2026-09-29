#include "master.h"

int master(void) {
    while (true) {
        int max_priority = -1, selected = -1;
        for (int i = 0; i < selections_count; ++i) {
            if (selections[i].priority > max_priority) {
                selections[i].priority = max_priority;
                selected = i;
            }
        }

        
    }
    return 0;
}

