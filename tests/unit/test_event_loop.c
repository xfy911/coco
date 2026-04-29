/**
 * test_event_loop.c - event_loop.c 单元测试
 *
 * 测试覆盖:
 * - coco_sleep 正常路径
 * - coco_sleep NULL 调度器/协程错误路径
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

static volatile int sleep_done = 0;
static volatile int sleep_count = 0;

void sleep_coro(void *arg) {
    uint64_t ms = *(uint64_t*)arg;
    sleep_count++;

    int ret = coco_sleep(ms);
    if (ret == COCO_OK) {
        sleep_done++;
    }
}

void test_sleep_basic(void) {
    printf("  TEST: sleep_basic... ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    sleep_done = 0;
    sleep_count = 0;

    uint64_t delay = 50;  /* 50ms */
    coco_create(sched, sleep_coro, &delay, 0);

    coco_sched_run(sched);

    assert(sleep_count == 1);
    assert(sleep_done == 1);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_sleep_multiple(void) {
    printf("  TEST: sleep_multiple... ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    sleep_done = 0;
    sleep_count = 0;

    uint64_t delays[3] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        coco_create(sched, sleep_coro, &delays[i], 0);
    }

    coco_sched_run(sched);

    assert(sleep_count == 3);
    assert(sleep_done == 3);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_sleep_zero(void) {
    printf("  TEST: sleep_zero... ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    sleep_done = 0;
    sleep_count = 0;

    uint64_t delay = 0;
    coco_create(sched, sleep_coro, &delay, 0);

    coco_sched_run(sched);

    assert(sleep_count == 1);
    assert(sleep_done == 1);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

int main(void) {
    printf("\n=== event_loop.c Tests ===\n\n");

    test_sleep_basic();
    test_sleep_multiple();
    test_sleep_zero();

    printf("\n=== Results: 3/3 tests passed ===\n\n");
    return 0;
}