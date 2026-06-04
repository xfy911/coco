#include "coco.h"
#include <stdio.h>
#include <assert.h>

/* Forward declarations for platform functions */
int coco_preempt_block_signal(void);
int coco_preempt_unblock_signal(void);

void test_block_unblock(void *arg) {
    (void)arg;
    coco_preempt_enable();

    /* Block signals */
    int ret = coco_preempt_block_signal();
    assert(ret == COCO_OK);

    /* Sleep longer than the preempt interval (10ms).
     * If signals are truly blocked, preempt should NOT fire. */
    assert(coco_preempt_is_pending() == 0);
    coco_sleep(50);  /* 50ms > 10ms interval */
    assert(coco_preempt_is_pending() == 0);

    /* Unblock signals */
    ret = coco_preempt_unblock_signal();
    assert(ret == COCO_OK);

    /* Now preempt can fire again; yield to let it process */
    coco_yield();
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_create(sched, test_block_unblock, NULL, 0);
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    printf("test_preempt_block: PASSED\n");
    return 0;
}
