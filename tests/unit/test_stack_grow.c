/**
 * test_stack_grow.c - Stack growth functionality tests
 *
 * Tests stack growth calculation, frame pointer adjustment, and needs-growth check.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../src/coco_internal.h"
#include "coco_stack_grow.h"

/* ===== Test: Stack Growth Threshold Check ===== */

static void test_needs_stack_growth(void) {
    printf("Test: Stack growth threshold check\n");

    uintptr_t stack_base = 0x100000000;
    uintptr_t stack_limit = 0x100010000;  /* 64KB stack */

    /* SP far from base: no growth needed (only 25% used) */
    uintptr_t sp = 0x10000C000;
    assert(coco_needs_stack_growth(stack_base, stack_limit, sp) == false);

    /* SP at 50%: no growth needed */
    sp = 0x100008000;
    assert(coco_needs_stack_growth(stack_base, stack_limit, sp) == false);

    /* SP at 25% boundary: no growth needed (exactly at threshold) */
    sp = 0x100004000;
    assert(coco_needs_stack_growth(stack_base, stack_limit, sp) == false);

    /* SP below 25%: growth needed */
    sp = 0x100003000;
    assert(coco_needs_stack_growth(stack_base, stack_limit, sp) == true);

    /* SP very close to base: definitely needs growth */
    sp = 0x100001000;
    assert(coco_needs_stack_growth(stack_base, stack_limit, sp) == true);

    printf("  PASSED: Stack growth threshold check works\n");
}

/* ===== Test: New Stack Size Calculation ===== */

static void test_calc_new_stack_size(void) {
    printf("Test: New stack size calculation\n");

    size_t page_size = sysconf(_SC_PAGESIZE);

    /* Test doubling */
    size_t new_size = coco_calc_new_stack_size(4096);
    assert(new_size == 8192);  /* Doubled and page-aligned */

    new_size = coco_calc_new_stack_size(8192);
    assert(new_size == 16384);

    new_size = coco_calc_new_stack_size(16384);
    assert(new_size == 32768);

    /* Test at maximum - should return 0 */
    new_size = coco_calc_new_stack_size(COCO_STACK_MAX_SIZE);
    assert(new_size == 0);  /* Already at max, can't grow */

    /* Test near maximum - should clamp to max */
    new_size = coco_calc_new_stack_size(COCO_STACK_MAX_SIZE / 2);
    assert(new_size == COCO_STACK_MAX_SIZE);

    /* Test minimum size */
    new_size = coco_calc_new_stack_size(2048);
    assert(new_size == 4096);
    assert(new_size % page_size == 0);  /* Page-aligned */

    printf("  PASSED: Stack size calculation works\n");
}

/* ===== Test: Frame Pointer Adjustment ===== */

static void test_adjust_frame_pointers(void) {
    printf("Test: Frame pointer adjustment\n");

    uintptr_t old_base = 0x100000000;
    uintptr_t old_size = 0x10000;  /* 64KB */
    uintptr_t new_base = 0x200000000;

    /* Test: FP inside old stack should be adjusted */
    uintptr_t saved_fp = old_base + 0x8000;
    uintptr_t saved_sp = old_base + 0x9000;

    coco_adjust_frame_pointers(old_base, old_size, new_base, &saved_fp, &saved_sp);

    /* FP and SP should be adjusted by delta */
    uintptr_t delta = new_base - old_base;
    assert(saved_fp == old_base + 0x8000 + delta);
    assert(saved_sp == old_base + 0x9000 + delta);

    /* Test: FP outside old stack should NOT be adjusted */
    saved_fp = 0x300000000;  /* Outside range */
    saved_sp = 0x300010000;
    coco_adjust_frame_pointers(old_base, old_size, new_base, &saved_fp, &saved_sp);
    assert(saved_fp == 0x300000000);  /* Unchanged */
    assert(saved_sp == 0x300010000);  /* Unchanged */

    /* Test: FP at boundary */
    saved_fp = old_base;  /* At base */
    saved_sp = old_base;
    coco_adjust_frame_pointers(old_base, old_size, new_base, &saved_fp, &saved_sp);
    assert(saved_fp == old_base + delta);
    assert(saved_sp == old_base + delta);

    printf("  PASSED: Frame pointer adjustment works\n");
}

/* ===== Test: Stack Grow with mmap ===== */

static void test_grow_stack_basic(void) {
    printf("Test: Basic stack grow with mmap\n");

    /* Allocate initial stack */
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t initial_size = 4096;

    void *raw_alloc = mmap(NULL, initial_size + page_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(raw_alloc != MAP_FAILED);

    /* Set guard page */
    mprotect(raw_alloc, page_size, PROT_NONE);

    uintptr_t old_base = (uintptr_t)raw_alloc + page_size;
    uintptr_t old_limit = old_base + initial_size;

    /* Setup context */
    struct coco_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.stack_base = (void *)old_base;
    ctx.stack_limit = (void *)old_limit;
    ctx.sp = (void *)(old_base + initial_size / 2);  /* SP in middle */
    ctx.fp = (void *)(old_base + initial_size / 2 + 16);

    /* Grow the stack */
    coco_grow_info_t info = coco_grow_stack(
        &ctx,
        NULL,  /* No stack map */
        (uintptr_t)ctx.sp,
        false,  /* Not from pool */
        NULL,   /* No pool */
        1,      /* Coroutine ID */
        false   /* Not growable */
    );

    /* Growth should succeed */
    assert(info.result == COCO_GROW_OK);
    assert(info.new_size > info.old_size);  /* New stack is larger */
    assert(info.new_size == initial_size * 2);  /* Doubled */

    /* Context should be updated */
    assert(ctx.stack_base == (void *)info.new_base);
    assert(ctx.stack_limit == (void *)info.new_limit);

    /* Clean up new stack */
    munmap((void *)(info.new_base - page_size), info.new_size + page_size);
    /* Clean up old stack (already freed by coco_grow_stack when not from pool) */

    printf("  PASSED: Basic stack grow works\n");
}

static void test_grow_stack_max_reached(void) {
    printf("Test: Stack grow at maximum size fails\n");

    /* Test: coco_calc_new_stack_size returns 0 at max */
    size_t new_size = coco_calc_new_stack_size(COCO_STACK_MAX_SIZE);
    assert(new_size == 0);

    /* Test: coco_calc_new_stack_size clamps near max */
    new_size = coco_calc_new_stack_size(COCO_STACK_MAX_SIZE / 2);
    assert(new_size == COCO_STACK_MAX_SIZE);

    printf("  PASSED: Stack grow at max size correctly handled\n");
}

static void test_adjust_stack_pointers(void) {
    printf("Test: Stack pointer adjustment (no stack map)\n");

    /* Setup old and new stacks */
    uintptr_t old_base = 0x100000000;
    uintptr_t old_limit = 0x100010000;
    uintptr_t new_base = 0x200000000;
    uintptr_t new_limit = 0x200010000;
    uintptr_t saved_fp = old_base + 0x8000;
    uintptr_t saved_sp = old_base + 0x9000;

    /* Without stack map, should still work (no pointers to adjust) */
    coco_adjust_stack_pointers(
        old_base, old_limit, new_base, new_limit,
        NULL,  /* No stack map */
        saved_fp, saved_sp
    );

    printf("  PASSED: Stack pointer adjustment handles NULL map\n");
}

int main(void) {
    printf("Coco Stack Growth Tests\n");
    printf("=======================\n\n");

    test_needs_stack_growth();
    test_calc_new_stack_size();
    test_adjust_frame_pointers();
    test_grow_stack_basic();
    test_grow_stack_max_reached();
    test_adjust_stack_pointers();

    printf("\n=== All Stack Growth Tests Passed ===\n");
    return 0;
}
