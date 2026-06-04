#include "coco.h"
#include <stdio.h>
#include <assert.h>

static void dummy(void *arg) {
    (void)arg;
}

int main(void) {
    /* First start/stop cycle */
    coco_global_sched_start(2);
    coco_go(dummy, NULL);
    coco_global_sched_wait();
    coco_global_sched_stop();

    /* Second start/stop cycle — must work correctly */
    coco_global_sched_start(2);
    coco_go(dummy, NULL);
    coco_global_sched_wait();
    coco_global_sched_stop();

    printf("test_sched_reinit: PASSED\n");
    return 0;
}
