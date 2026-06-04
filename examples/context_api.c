#include "coco.h"
#include <stdio.h>

static void background_task(void *arg) {
    (void)arg;
    printf("context_api: background task running\n");
    for (int i = 0; i < 3; i++) {
        printf("context_api: tick %d\n", i);
        coco_sleep(100);
    }
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    printf("context_api: coco version %s\n", coco_version());
    coco_create(sched, background_task, NULL, 0);
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    printf("context_api: done\n");
    return 0;
}
