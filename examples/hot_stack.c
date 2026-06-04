#include "coco.h"
#include <stdio.h>

static void small_task(void *arg) {
    int id = (int)(intptr_t)arg;
    char buf[1024];
    (void)buf;
    printf("hot_stack: coroutine %d running with shared stack\n", id);
    coco_yield();
    printf("hot_stack: coroutine %d resumed\n", id);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    /* Default stack size (0) uses shared hot stack */
    for (int i = 0; i < 16; i++) {
        coco_create(sched, small_task, (void *)(intptr_t)i, 0);
    }
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    printf("hot_stack: done\n");
    return 0;
}
