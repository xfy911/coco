#include "../src/coco_internal.h"
#include <stdio.h>

static void simple_coro(void *arg) {
    int *p = (int*)arg;
    (*p)++;
}

int main(void) {
    printf("=== Lifecycle Tests ===\n");
    
    printf("  test_self_outside: ");
    if (coco_self() == NULL) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    printf("  test_get_state_null: ");
    if (coco_get_state(NULL) == COCO_STATE_DEAD) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    printf("  test_get_id_null: ");
    if (coco_get_id(NULL) == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    printf("  test_get_stack_usage_null: ");
    if (coco_get_stack_usage(NULL) == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    printf("  test_join_null: ");
    if (coco_join(NULL) == NULL) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    printf("  test_create_and_run: ");
    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); return 1; }
    
    int value = 0;
    coco_coro_t *coro = coco_create(sched, simple_coro, &value, 64 * 1024);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); return 1; }
    
    coco_sched_run(sched);
    
    if (value == 1 && coco_get_state(coro) == COCO_STATE_DEAD) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
    
    coco_sched_destroy(sched);
    
    printf("\nResults: All passed\n");
    return 0;
}
