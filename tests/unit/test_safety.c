/**
 * test_safety.c - Safety mode implementation tests
 *
 * Tests safety mode configuration, stack shrink check, and pointer scanning.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "../src/coco_internal.h"
#include "coco_safety.h"

/* ===== Test: Safety Mode Configuration ===== */

static void test_safety_mode_config(void) {
    printf("Test: Safety mode configuration\n");

    /* Test NONE mode */
    coco_safety_config_t config_none = coco_get_default_config(COCO_SAFETY_NONE);
    assert(config_none.mode == COCO_SAFETY_NONE);
    assert(config_none.grow_threshold_percent == 100);  /* Never grow */
    assert(config_none.auto_shrink == false);
    assert(config_none.scan_all_pointers == false);

    /* Test CONSERVATIVE mode */
    coco_safety_config_t config_conservative = coco_get_default_config(COCO_SAFETY_CONSERVATIVE);
    assert(config_conservative.mode == COCO_SAFETY_CONSERVATIVE);
    assert(config_conservative.max_stack_size == COCO_STACK_CONSERVATIVE_MAX);
    assert(config_conservative.grow_threshold_percent == 75);
    assert(config_conservative.auto_shrink == false);
    assert(config_conservative.scan_all_pointers == false);

    /* Test FULL mode */
    coco_safety_config_t config_full = coco_get_default_config(COCO_SAFETY_FULL);
    assert(config_full.mode == COCO_SAFETY_FULL);
    assert(config_full.max_stack_size == COCO_STACK_MAX_SIZE);
    assert(config_full.grow_threshold_percent == 75);
    assert(config_full.shrink_threshold_percent == 25);
    assert(config_full.auto_shrink == true);
    assert(config_full.scan_all_pointers == true);

    printf("  PASSED: Safety mode configurations correct\n");
}

/* ===== Test: Global Safety Mode ===== */

static void test_global_safety_mode(void) {
    printf("Test: Global safety mode get/set\n");

    /* Save current mode */
    coco_safety_mode_t prev = coco_get_safety_mode();

    /* Test set/get */
    coco_safety_mode_t old = coco_set_safety_mode(COCO_SAFETY_CONSERVATIVE);
    assert(old == prev);
    assert(coco_get_safety_mode() == COCO_SAFETY_CONSERVATIVE);

    old = coco_set_safety_mode(COCO_SAFETY_FULL);
    assert(old == COCO_SAFETY_CONSERVATIVE);
    assert(coco_get_safety_mode() == COCO_SAFETY_FULL);

    /* Restore */
    coco_set_safety_mode(prev);

    printf("  PASSED: Global safety mode get/set works\n");
}

/* ===== Test: Safety Mode Coroutine Creation ===== */

static void safety_coro_entry(void *arg) {
    int *result = (int *)arg;
    *result = 42;
}

static void test_create_safe_coro(void) {
    printf("Test: Create coroutine with safety modes\n");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    /* Save and set safety mode */
    coco_safety_mode_t prev = coco_get_safety_mode();

    /* Test NONE mode */
    coco_set_safety_mode(COCO_SAFETY_NONE);
    coco_coro_t *coro_none = coco_create_safe(sched, safety_coro_entry, NULL, 0, COCO_SAFETY_NONE);
    assert(coro_none != NULL);
    assert(coro_none->safety_mode == COCO_SAFETY_NONE);
    assert(coro_none->stack_growable == false);
    assert(coro_none->max_stack_size == COCO_STACK_CONSERVATIVE);  /* Fixed size */

    /* Test CONSERVATIVE mode */
    coco_coro_t *coro_conservative = coco_create_safe(sched, safety_coro_entry, NULL, 0, COCO_SAFETY_CONSERVATIVE);
    assert(coro_conservative != NULL);
    assert(coro_conservative->safety_mode == COCO_SAFETY_CONSERVATIVE);
    assert(coro_conservative->stack_growable == true);
    assert(coro_conservative->max_stack_size == COCO_STACK_CONSERVATIVE_MAX);

    /* Test FULL mode */
    coco_coro_t *coro_full = coco_create_safe(sched, safety_coro_entry, NULL, 0, COCO_SAFETY_FULL);
    assert(coro_full != NULL);
    assert(coro_full->safety_mode == COCO_SAFETY_FULL);
    assert(coro_full->stack_growable == true);
    assert(coro_full->max_stack_size == COCO_STACK_MAX_SIZE);

    /* Test with custom stack size */
    coco_coro_t *coro_custom = coco_create_safe(sched, safety_coro_entry, NULL, 8192, COCO_SAFETY_FULL);
    assert(coro_custom != NULL);
    assert(coro_custom->current_stack_size == 8192);

    /* Restore */
    coco_set_safety_mode(prev);

    coco_sched_destroy(sched);

    printf("  PASSED: Safety mode coroutine creation works\n");
}

/* ===== Test: Stack Shrink Logic ===== */

static void test_can_shrink_stack(void) {
    printf("Test: Stack shrink check\n");

    /* Create a scheduler and coroutine */
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create_safe(sched, safety_coro_entry, NULL, 0, COCO_SAFETY_FULL);
    assert(coro != NULL);

    /* Configure shrink parameters */
    coco_safety_config_t config = coco_get_default_config(COCO_SAFETY_FULL);

    /* Setup mock stack bounds */
    coro->ctx.stack_base = (void *)0x100000000;
    coro->ctx.stack_limit = (void *)0x100010000;  /* 64KB stack */
    coro->ctx.sp = (void *)0x100008000;  /* Halfway used */

    /* Should not shrink when above threshold */
    bool can_shrink = coco_can_shrink_stack(coro, &config);
    assert(can_shrink == false);

    /* Move SP closer to base (low usage) */
    coro->ctx.sp = (void *)0x10000F000;  /* Only 4KB used of 64KB (6.25%) */
    can_shrink = coco_can_shrink_stack(coro, &config);
    assert(can_shrink == true);

    /* Test with shrink disabled */
    config.auto_shrink = false;
    can_shrink = coco_can_shrink_stack(coro, &config);
    assert(can_shrink == false);

    /* Test invalid bounds */
    coro->ctx.stack_base = NULL;
    coro->ctx.stack_limit = NULL;
    can_shrink = coco_can_shrink_stack(coro, &config);
    assert(can_shrink == false);

    coco_sched_destroy(sched);

    printf("  PASSED: Stack shrink check works correctly\n");
}

/* ===== Test: Pointer Scanning ===== */

static uint32_t visitor_count = 0;

static bool test_pointer_visitor(uintptr_t ptr_addr, uintptr_t ptr_value,
                                  const coco_ptr_desc_t *desc,
                                  const coco_frame_info_t *frame_info,
                                  void *user_data) {
    (void)ptr_addr;
    (void)desc;
    (void)frame_info;
    (void)user_data;

    visitor_count++;
    return true;
}

static bool test_pointer_visitor_stop(uintptr_t ptr_addr, uintptr_t ptr_value,
                                       const coco_ptr_desc_t *desc,
                                       const coco_frame_info_t *frame_info,
                                       void *user_data) {
    (void)ptr_addr;
    (void)ptr_value;
    (void)desc;
    (void)frame_info;
    (void)user_data;

    visitor_count++;
    return false;  /* Stop after first */
}

static void test_stack_pointer_scan(void) {
    printf("Test: Stack pointer scanning\n");

    /* Setup mock stack with some pointer-like values */
    size_t stack_size = 4096;
    uintptr_t *mock_stack = (uintptr_t *)calloc(stack_size / sizeof(uintptr_t), sizeof(uintptr_t));
    assert(mock_stack != NULL);

    uintptr_t stack_base = (uintptr_t)mock_stack;
    uintptr_t stack_limit = stack_base + stack_size;
    uintptr_t current_sp = stack_base;

    /* Place some pointer-like values in the stack */
    mock_stack[10] = stack_base + 2048;  /* Valid pointer */
    mock_stack[20] = stack_base + 3072;  /* Valid pointer */
    mock_stack[30] = 0xDEADBEEF;          /* Invalid pointer */
    mock_stack[40] = stack_base + 1024;  /* Valid pointer */

    /* Count all potential pointers */
    visitor_count = 0;
    uint32_t count = coco_scan_stack_pointers(stack_base, stack_limit, current_sp,
                                               test_pointer_visitor, (void *)stack_base);
    assert(count > 0);
    printf("  Found %u potential pointers\n", count);

    /* Test early termination */
    visitor_count = 0;
    count = coco_scan_stack_pointers(stack_base, stack_limit, current_sp,
                                      test_pointer_visitor_stop, (void *)stack_base);
    assert(visitor_count == 1);  /* Should stop after first */

    free(mock_stack);

    printf("  PASSED: Stack pointer scanning works\n");
}

/* ===== Test: Error Cases ===== */

static void test_safety_error_cases(void) {
    printf("Test: Safety mode error handling\n");

    /* Test NULL scheduler */
    coco_coro_t *coro = coco_create_safe(NULL, safety_coro_entry, NULL, 0, COCO_SAFETY_NONE);
    assert(coro == NULL);

    /* Test NULL entry */
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coro = coco_create_safe(sched, NULL, NULL, 0, COCO_SAFETY_NONE);
    assert(coro == NULL);

    coco_sched_destroy(sched);

    printf("  PASSED: Error handling works correctly\n");
}

int main(void) {
    printf("Coco Safety Mode Tests\n");
    printf("======================\n\n");

    test_safety_mode_config();
    test_global_safety_mode();
    test_create_safe_coro();
    test_can_shrink_stack();
    test_stack_pointer_scan();
    test_safety_error_cases();

    printf("\n=== All Safety Tests Passed ===\n");
    return 0;
}
