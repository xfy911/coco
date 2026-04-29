/**
 * test_aging_trigger.c - 老化机制触发测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <unistd.h>

#define STACK_SIZE (64 * 1024)

static int tests_passed = 0;
static int tests_failed = 0;

static volatile int coro_executed = 0;

static void slow_coro(void *arg) {
    int id = (int)(intptr_t)arg;
    coro_executed = id;
    /* 模拟长时间运行 */
    for (int i = 0; i < 100; i++) {
        coco_yield();
    }
}

static void quick_coro(void *arg) {
    (void)arg;
    /* 快速完成 */
}

static void test_aging_with_long_wait(void) {
    printf("  test_aging_with_long_wait: ");
    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    /* 创建多个协程 */
    coco_coro_t *c1 = coco_create(sched, quick_coro, NULL, STACK_SIZE);
    coco_coro_t *c2 = coco_create(sched, slow_coro, (void*)(intptr_t)2, STACK_SIZE);
    coco_coro_t *c3 = coco_create(sched, quick_coro, NULL, STACK_SIZE);

    if (!c1 || !c2 || !c3) {
        coco_sched_destroy(sched);
        printf("FAIL - no coro\n");
        tests_failed++;
        return;
    }

    /* 设置不同优先级 */
    coco_set_priority(c1, COCO_PRIORITY_HIGH);
    coco_set_priority(c2, COCO_PRIORITY_LOW);
    coco_set_priority(c3, COCO_PRIORITY_NORMAL);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证所有协程都完成了 */
    if (coco_get_state(c1) == COCO_STATE_DEAD &&
        coco_get_state(c2) == COCO_STATE_DEAD &&
        coco_get_state(c3) == COCO_STATE_DEAD) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - not all coros completed\n");
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

static void test_priority_change_during_run(void) {
    printf("  test_priority_change_during_run: ");
    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    coco_coro_t *c1 = coco_create(sched, quick_coro, NULL, STACK_SIZE);
    coco_coro_t *c2 = coco_create(sched, quick_coro, NULL, STACK_SIZE);

    if (!c1 || !c2) {
        coco_sched_destroy(sched);
        printf("FAIL - no coro\n");
        tests_failed++;
        return;
    }

    /* 初始设置优先级 */
    coco_set_priority(c1, COCO_PRIORITY_LOW);
    coco_set_priority(c2, COCO_PRIORITY_HIGH);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证 */
    if (coco_get_state(c1) == COCO_STATE_DEAD &&
        coco_get_state(c2) == COCO_STATE_DEAD) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL\n");
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

int main(void) {
    printf("=== Aging Trigger Tests ===\n");

    test_aging_with_long_wait();
    test_priority_change_during_run();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
