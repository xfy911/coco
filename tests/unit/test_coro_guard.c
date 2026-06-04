#include "coco.h"
#include <stdio.h>
#include <assert.h>

static void dummy(void *arg) {
    (void)arg;
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_create(sched, dummy, NULL, 0);
    coco_sched_run(sched);

    /* Calling yield outside coroutine should return error, not crash */
    int ret = coco_yield();
    assert(ret == COCO_ERROR_INVALID);

    ret = coco_sleep(1);
    assert(ret == COCO_ERROR_INVALID);

    coco_channel_t *ch = coco_channel_create(1);
    ret = coco_channel_send(ch, (void *)1);
    assert(ret == COCO_ERROR_INVALID);

    void *val;
    ret = coco_channel_recv(ch, &val);
    assert(ret == COCO_ERROR_INVALID);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("test_coro_guard: PASSED\n");
    return 0;
}
