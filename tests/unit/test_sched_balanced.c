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
    /* 注入大量协程，负载均衡应将其分布到各处理器 */
    for (int i = 0; i < 200; i++) {
        coco_go(worker, NULL);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();
    assert(counter == 200 * 100);
    printf("test_sched_balanced: PASSED (counter=%d)\n", counter);
    return 0;
}
