/**
 * test_lifecycle.c - 协程生命周期测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>

#define STACK_SIZE (64 * 1024)

static int test_passed = 0;
static int test_failed = 0;

static void simple_coro(void *arg) {
    int *p = (int*)arg;
    (*p)++;
}

int main(void) {
    printf("=== Lifecycle Tests ===\n");

    /* test_join_basic */
    printf("  test_join_basic: ");
    {
        coco_sched_t *sched = coco_sched_create();
        if (!sched) { printf("FAIL - no sched\n"); test_failed++; goto next1; }

        int value = 0;
        coco_coro_t *coro = coco_create(sched, simple_coro, &value, STACK_SIZE);
        if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); test_failed++; goto next1; }

        coco_sched_run(sched);

        if (value == 1 && coco_get_state(coro) == COCO_STATE_DEAD) {
            printf("PASS\n");
            test_passed++;
        } else {
            printf("FAIL - value=%d state=%d\n", value, coco_get_state(coro));
            test_failed++;
        }

        coco_sched_destroy(sched);
    }
next1:

    /* test_self_outside_coro */
    printf("  test_self_outside_coro: ");
    if (coco_self() == NULL) {
        printf("PASS\n");
        test_passed++;
    } else {
        printf("FAIL\n");
        test_failed++;
    }

    /* test_get_state_null */
    printf("  test_get_state_null: ");
    if (coco_get_state(NULL) == COCO_STATE_DEAD) {
        printf("PASS\n");
        test_passed++;
    } else {
        printf("FAIL\n");
        test_failed++;
    }

    printf("\nResults: %d passed, %d failed\n", test_passed, test_failed);
    return test_failed > 0 ? 1 : 0;
}
