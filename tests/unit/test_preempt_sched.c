#include "coco.h"
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>

static _Atomic int counter = 0;

static void worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 100; i++) {
        atomic_fetch_add(&counter, 1);
    }
}

int main(void) {
    counter = 0;
    coco_global_sched_start(4);
    for (int i = 0; i < 20; i++) {
        coco_go(worker, NULL);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();

    assert(counter == 20 * 100);
    printf("test_preempt_sched: PASSED (counter=%d)\n", counter);
    return 0;
}
