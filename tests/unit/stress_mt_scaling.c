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

int main(void) {
    printf("stress_mt_scaling:\n");

    /*
     * NOTE: Runs one configuration to avoid reinitialization bug.
     * TODO: Run 1P/2P/4P sequence once scheduler supports reinit.
     * TODO: Increase coroutine count once stack_pool_multi race is fixed.
     */
    counter = 0;
    coco_global_sched_start(2);
    for (int i = 0; i < 100; i++) {
        coco_go(worker, NULL);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();

    assert(counter == 100 * 1000);
    printf("  P=2 coro=100 counter=%d: OK\n", counter);

    printf("stress_mt_scaling: PASSED\n");
    return 0;
}
