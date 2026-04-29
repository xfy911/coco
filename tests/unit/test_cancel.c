/**
 * test_cancel.c - 协程取消测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define STACK_SIZE (64 * 1024)

/* 测试数据 */
static volatile int cancel_flag = 0;
static volatile int coro_executed = 0;

/* 简单协程：检查取消状态 */
static void cancellable_coro(void *arg) {
    coro_executed = 1;
    while (!coco_cancelled()) {
        coco_yield();
    }
    cancel_flag = 1;
}

/* 快速完成的协程 */
static void quick_coro(void *arg) {
    (void)arg;
    /* 快速完成 */
}

/* 死协程测试：先运行完成再尝试取消 */
static void dead_coro(void *arg) {
    (void)arg;
    /* 立即返回 */
}

/* 测试取消 NULL 协程 */
static void test_cancel_null(void) {
    printf("  test_cancel_null: ");
    int ret = coco_cancel(NULL);
    if (ret == COCO_ERROR) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - expected COCO_ERROR\n");
        tests_failed++;
    }
}

/* 测试在协程外调用 coco_cancelled */
static void test_cancelled_outside(void) {
    printf("  test_cancelled_outside: ");
    int cancelled = coco_cancelled();
    if (cancelled == 0) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - expected 0\n");
        tests_failed++;
    }
}

/* 测试取消正在运行的协程 */
static void test_cancel_running(void) {
    printf("  test_cancel_running: ");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    cancel_flag = 0;
    coro_executed = 0;

    coco_coro_t *coro = coco_create(sched, cancellable_coro, NULL, STACK_SIZE);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); tests_failed++; return; }

    /* 运行一次让协程开始执行 */
    coco_sched_run_once(sched);

    if (!coro_executed) {
        coco_sched_destroy(sched);
        printf("FAIL - coro not executed\n");
        tests_failed++;
        return;
    }

    /* 取消协程 */
    int ret = coco_cancel(coro);
    if (ret != COCO_OK) {
        coco_sched_destroy(sched);
        printf("FAIL - cancel returned %d\n", ret);
        tests_failed++;
        return;
    }

    /* 继续运行让协程检查取消状态 */
    coco_sched_run_once(sched);

    if (cancel_flag) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - coro did not detect cancellation\n");
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

/* 测试取消已完成的协程 */
static void test_cancel_dead(void) {
    printf("  test_cancel_dead: ");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    coco_coro_t *coro = coco_create(sched, dead_coro, NULL, STACK_SIZE);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); tests_failed++; return; }

    /* 运行完成 */
    coco_sched_run(sched);

    /* 尝试取消已完成的协程 */
    int ret = coco_cancel(coro);

    if (ret == COCO_ERROR) {
        printf("PASS\n");
        tests_passed++;
    } else {
        printf("FAIL - expected COCO_ERROR for dead coro\n");
        tests_failed++;
    }

    coco_sched_destroy(sched);
}

/* 测试在协程内检查取消状态 */
static void test_cancelled_inside(void) {
    printf("  test_cancelled_inside: ");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) { printf("FAIL - no sched\n"); tests_failed++; return; }

    cancel_flag = 0;
    coro_executed = 0;

    coco_coro_t *coro = coco_create(sched, cancellable_coro, NULL, STACK_SIZE);
    if (!coro) { coco_sched_destroy(sched); printf("FAIL - no coro\n"); tests_failed++; return; }

    /* 运行一次 */
    coco_sched_run_once(sched);

    /* 在协程外，coco_cancelled 应返回 0 */
    int cancelled = coco_cancelled();
    if (cancelled != 0) {
        coco_sched_destroy(sched);
        printf("FAIL - coco_cancelled returned %d outside coro\n", cancelled);
        tests_failed++;
        return;
    }

    /* 取消并运行完成 */
    coco_cancel(coro);
    coco_sched_run(sched);

    printf("PASS\n");
    tests_passed++;
    coco_sched_destroy(sched);
}

int main(void) {
    printf("=== Cancel Tests ===\n");

    test_cancel_null();
    test_cancelled_outside();
    test_cancel_running();
    test_cancel_dead();
    test_cancelled_inside();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
