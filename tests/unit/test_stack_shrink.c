/**
 * test_stack_shrink.c - Stack shrink tests
 *
 * Tests coco_can_shrink_stack() and coco_shrink_stack() from the
 * safety API (coco_safety.h).
 *
 * Only uses public API functions - no direct struct field access.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "coco.h"
#include "coco_safety.h"

/* Simple coroutine entry - just exits */
static void simple_entry(void *arg) {
    (void)arg;
}

int main(void) {
    printf("Stack shrink tests:\n");
    int pass_count = 0;
    int fail_count = 0;

    /* Test 1: shrink_stack returns false for NULL coro */
    printf("  test_shrink_null_coro... ");
    {
        coco_safety_config_t config = coco_get_default_config(COCO_SAFETY_FULL);
        bool result = coco_shrink_stack(NULL, NULL, &config);
        if (result == false) {
            printf("PASSED\n"); pass_count++;
        } else {
            printf("FAILED\n"); fail_count++;
        }
    }

    /* Test 2: can_shrink returns false when auto_shrink is disabled */
    printf("  test_can_shrink_no_auto... ");
    {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);

        coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 256 * 1024);
        assert(coro != NULL);

        coco_safety_config_t config = coco_get_default_config(COCO_SAFETY_CONSERVATIVE);
        assert(config.auto_shrink == false);

        bool can = coco_can_shrink_stack(coro, &config);
        if (can == false) {
            printf("PASSED\n"); pass_count++;
        } else {
            printf("FAILED\n"); fail_count++;
        }

        coco_sched_run(sched);
        coco_sched_destroy(sched);
    }

    /* Test 3: shrink_stack returns false when auto_shrink is disabled */
    printf("  test_shrink_when_cannot... ");
    {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);

        coco_coro_t *coro = coco_create(sched, simple_entry, NULL, COCO_DEFAULT_STACK_SIZE);
        assert(coro != NULL);

        coco_safety_config_t config = coco_get_default_config(COCO_SAFETY_CONSERVATIVE);
        bool result = coco_shrink_stack(coro, NULL, &config);
        if (result == false) {
            printf("PASSED\n"); pass_count++;
        } else {
            printf("FAILED\n"); fail_count++;
        }

        coco_sched_run(sched);
        coco_sched_destroy(sched);
    }

    /* Test 4: coco_create_safe with FULL safety - verify can_shrink works */
    printf("  test_create_safe_can_shrink... ");
    {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);

        coco_coro_t *coro = coco_create_safe(sched, simple_entry, NULL, 256 * 1024,
                                              COCO_SAFETY_FULL);
        assert(coro != NULL);

        coco_safety_config_t config = coco_get_default_config(COCO_SAFETY_FULL);
        assert(config.auto_shrink == true);

        /* Verify can_shrink_stack does not crash.
           The return value depends on ctx.stack_base/stack_limit initialization. */
        bool can = coco_can_shrink_stack(coro, &config);
        (void)can;  /* We just verify no crash */

        /* Verify shrink_stack does not crash */
        bool shrunk = coco_shrink_stack(coro, NULL, &config);
        (void)shrunk;  /* We just verify no crash */

        printf("PASSED\n"); pass_count++;

        coco_sched_run(sched);
        coco_sched_destroy(sched);
    }

    /* Test 5: config defaults are correct */
    printf("  test_config_defaults... ");
    {
        coco_safety_config_t none = coco_get_default_config(COCO_SAFETY_NONE);
        coco_safety_config_t cons = coco_get_default_config(COCO_SAFETY_CONSERVATIVE);
        coco_safety_config_t full = coco_get_default_config(COCO_SAFETY_FULL);

        bool ok = true;
        ok = ok && (none.auto_shrink == false);
        ok = ok && (cons.auto_shrink == false);
        ok = ok && (full.auto_shrink == true);
        ok = ok && (full.shrink_threshold_percent == 25);
        ok = ok && (full.min_stack_size == COCO_STACK_MIN_SIZE);

        if (ok) {
            printf("PASSED\n"); pass_count++;
        } else {
            printf("FAILED\n"); fail_count++;
        }
    }

    /* Test 6: can_shrink returns false for regular coco_create coroutine
       (without create_safe, ctx.stack_base/limit are 0) */
    printf("  test_can_shrink_no_bounds... ");
    {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);

        coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 256 * 1024);
        assert(coro != NULL);

        coco_safety_config_t config = coco_get_default_config(COCO_SAFETY_FULL);
        /* Regular coco_create leaves ctx.stack_base/limit as 0,
           so can_shrink should return false */
        bool can = coco_can_shrink_stack(coro, &config);
        if (can == false) {
            printf("PASSED\n"); pass_count++;
        } else {
            printf("FAILED\n"); fail_count++;
        }

        coco_sched_run(sched);
        coco_sched_destroy(sched);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
