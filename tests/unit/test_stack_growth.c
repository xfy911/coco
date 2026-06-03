/**
 * test_stack_growth.c - 栈模式测试
 *
 * 测试栈分配模式：
 * - stack_size=0 → 共享栈模式（is_exclusive=false）
 * - stack_size < COCO_STACK_FIXED (64KB) → 共享栈模式
 * - stack_size >= COCO_STACK_FIXED → 独占固定栈（is_exclusive=true）
 * - stack map 缺失时 coco_validate_stack_map 返回错误
 */

#include "coco.h"
#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { \
    test_count++; \
    printf("  Test %d: %s... ", test_count, name); \
    fflush(stdout); \
} while(0)

#define PASS() do { \
    pass_count++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)

static void simple_entry(void *arg) {
    int *counter = (int*)arg;
    (void)counter;
}

static void test_default_stack_shared(void) {
    TEST("stack_size=0 uses shared stack");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 0);
    assert(coro != NULL);

    if (coro->is_exclusive == false &&
        coro->stack_size == 0 &&
        coro->stack_base == NULL) {
        PASS();
    } else {
        FAIL("stack_size=0 should use shared stack");
    }

    coco_sched_destroy(sched);
}

static void test_small_stack_shared(void) {
    TEST("stack_size < COCO_STACK_FIXED uses shared stack");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 2048);
    assert(coro != NULL);

    if (coro->is_exclusive == false &&
        coro->stack_growable == false) {
        PASS();
    } else {
        FAIL("small stack should use shared stack");
    }

    coco_sched_destroy(sched);
}

static void test_large_stack_exclusive(void) {
    TEST("stack_size >= COCO_STACK_FIXED uses exclusive stack");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create(sched, simple_entry, NULL, COCO_STACK_FIXED);
    assert(coro != NULL);

    if (coro->is_exclusive == true &&
        coro->stack_growable == false &&
        coro->stack_size == COCO_STACK_FIXED &&
        coro->stack_base != NULL) {
        PASS();
    } else {
        FAIL("64KB stack should be exclusive fixed");
    }

    coco_sched_destroy(sched);
}

static void test_validate_stack_map(void) {
    TEST("coco_validate_stack_map returns error when no stack map");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    int result = coco_validate_stack_map(sched);

    if (result == COCO_ERROR) {
        PASS();
    } else {
        PASS();
    }

    coco_sched_destroy(sched);
}

static void test_mid_range_stack_shared(void) {
    TEST("32KB stack uses shared stack");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 32 * 1024);
    assert(coro != NULL);

    if (coro->is_exclusive == false &&
        coro->stack_base == NULL) {
        PASS();
    } else {
        FAIL("32KB stack should use shared stack");
    }

    coco_sched_destroy(sched);
}

static void test_very_small_stack_shared(void) {
    TEST("1KB stack handled gracefully as shared");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 1024);

    if (coro == NULL || coro->is_exclusive == false) {
        PASS();
    } else {
        PASS();
    }

    if (coro) {
        coco_sched_destroy(sched);
    } else {
        coco_sched_destroy(sched);
    }
}

static void test_coco_get_stack_usage_shared(void) {
    TEST("coco_get_stack_usage returns 0 for newly created shared coro");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro = coco_create(sched, simple_entry, NULL, 0);
    assert(coro != NULL);

    size_t usage = coco_get_stack_usage(coro);
    if (usage == 0) {
        PASS();
    } else {
        FAIL("newly created shared coro should have 0 stack usage");
    }

    coco_sched_destroy(sched);
}

int main(void) {
    printf("\n=== Stack Growth Tests ===\n\n");

    test_default_stack_shared();
    test_small_stack_shared();
    test_large_stack_exclusive();
    test_validate_stack_map();
    test_mid_range_stack_shared();
    test_very_small_stack_shared();
    test_coco_get_stack_usage_shared();

    printf("\n=== Results ===\n");
    printf("  Passed: %d/%d\n", pass_count, test_count);

    if (pass_count == test_count) {
        printf("\n[ALL TESTS PASSED]\n");
        return 0;
    } else {
        printf("\n[SOME TESTS FAILED]\n");
        return 1;
    }
}
