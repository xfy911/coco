#include "coco.h"
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>

static _Atomic int counter = 0;

static void worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        atomic_fetch_add(&counter, 1);
    }
}

static void run_config(int nproc, int ncoro) {
    counter = 0;
    coco_global_sched_start(nproc);
    for (int i = 0; i < ncoro; i++) {
        coco_go(worker, NULL);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();

    assert(counter == ncoro * 1000);
    printf("  P=%d coro=%d counter=%d: OK\n", nproc, ncoro, counter);
}

int main(void) {
    printf("stress_mt_scaling:\n");

    run_config(1, 100);
    run_config(2, 100);
    run_config(4, 100);

    printf("stress_mt_scaling: PASSED\n");
    return 0;
}
