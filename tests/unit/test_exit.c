/**
 * test_exit.c - coco_exit 测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>

#define STACK_SIZE (64 * 1024)

static int tests_passed = 0;
static int tests_failed = 0;

static void *exit_result = NULL;

static void exit_coro(void *arg) {
    coco_exit(coco_self(), arg);
    /* 不应该到达这里 */
}

static void normal_coro(void *arg) {
    int *p = (int*)arg;
    (*p)++;
}

static void test_exit_with_result(void) {
    printf("  test_exit_with_result: ");
    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    exit_result = (void*)0x1234;
    coco_coro_t *coro = coco_create(sched, exit_coro, exit_result, STACK_SIZE);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); tests_failed++; return; }

    coco_sched_run(sched);

    if (coco_get_state(coro) == COCO_STATE_DEAD) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - state=%d\n", coco_get_state(coro));
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

static void test_exit_null_coro(void) {
    printf("  test_exit_null_coro: ");
    /* coco_exit(NULL, NULL) 应该安全返回 */
    coco_exit(NULL, NULL);
    printf("PASS\n");
    tests_passed++;
}

static void test_normal_completion(void) {
    printf("  test_normal_completion: ");
    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    int value = 0;
    coco_coro_t *coro = coco_create(sched, normal_coro, &value, STACK_SIZE);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); tests_failed++; return; }

    coco_sched_run(sched);

    if (value == 1 && coco_get_state(coro) == COCO_STATE_DEAD) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - value=%d state=%d\n", value, coco_get_state(coro));
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

int main(void) {
    printf("=== Exit Tests ===\n");

    test_exit_with_result();
    test_exit_null_coro();
    test_normal_completion();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
