/**
 * test_sleep.c - coco_sleep 测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>

#define STACK_SIZE (64 * 1024)

static int tests_passed = 0;
static int tests_failed = 0;

static volatile int sleep_done = 0;

static void sleep_coro(void *arg) {
    uint64_t ms = (uint64_t)(uintptr_t)arg;
    int ret = coco_sleep(ms);
    if (ret == COCO_OK) {
        sleep_done = 1;
    }
}

static void test_sleep_basic(void) {
    printf("  test_sleep_basic: ");
    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    sleep_done = 0;
    coco_coro_t *coro = coco_create(sched, sleep_coro, (void*)(uintptr_t)10, STACK_SIZE);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); tests_failed++; return; }

    coco_sched_run(sched);

    if (sleep_done) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - sleep not completed\n");
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

static void test_sleep_outside_coro(void) {
    printf("  test_sleep_outside_coro: ");
    int ret = coco_sleep(10);
    if (ret == COCO_ERROR_INVALID) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - expected COCO_ERROR_INVALID\n");
        tests_failed++;
    }
}

int main(void) {
    printf("=== Sleep Tests ===\n");

    test_sleep_basic();
    test_sleep_outside_coro();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
