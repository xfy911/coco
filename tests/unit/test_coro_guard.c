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

    /* Test coco_join outside coroutine */
    {
        coco_coro_t *coro = coco_create(sched, dummy, NULL, 0);
        void *result = coco_join(coro);
        assert(result == NULL);
        /* Don't destroy here — coro is still in sched->coro_table;
         * let coco_sched_destroy clean it up to avoid double-free. */
    }

    /* Test coco_channel_select outside coroutine */
    {
        coco_select_case_t cases[1] = {{ch, COCO_SELECT_RECV, &val, 0}};
        int ret = coco_channel_select(cases, 1, 0, 0);
        assert(ret == COCO_ERROR_INVALID);
    }

    /* Test I/O APIs outside coroutine */
    {
        char buf[64];
        int ret = coco_read(-1, buf, sizeof(buf));
        assert(ret == COCO_ERROR_INVALID);
        ret = coco_write(-1, buf, sizeof(buf));
        assert(ret == COCO_ERROR_INVALID);
        ret = coco_accept(-1, NULL, NULL);
        assert(ret == COCO_ERROR_INVALID);
        ret = coco_connect(-1, NULL, 0);
        assert(ret == COCO_ERROR_INVALID);
    }

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("test_coro_guard: PASSED\n");
    return 0;
}
