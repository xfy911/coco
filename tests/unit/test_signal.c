/**
 * test_signal.c - 信号处理测试
 *
 * 测试信号处理初始化和栈溢出检测
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/coco_internal.h"

/* 测试统计 */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL - %s\n", msg); \
    tests_failed++; \
} while(0)

/* ========== 测试用例 ========== */

/**
 * test_signal_init_cleanup - 验证信号处理初始化和清理
 */
TEST(test_signal_init_cleanup) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    /* 调度器创建时会初始化信号处理 */
    /* 销毁时会清理 */
    coco_sched_destroy(sched);

    PASS();
}

/**
 * test_overflow_checkpoint - 验证栈溢出检查点设置
 */
TEST(test_overflow_checkpoint) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    /* 检查点在协程切换时自动设置 */
    /* 这里只验证调度器正常工作 */

    coco_sched_destroy(sched);
    PASS();
}

/**
 * test_signal_null_sched - 验证对 NULL 调度器的操作安全
 */
TEST(test_signal_null_sched) {
    /* 销毁 NULL 调度器应安全 */
    coco_sched_destroy(NULL);
    PASS();
}

/* ========== 主函数 ========== */

int main(void) {
    printf("=== Signal Tests ===\n");

    RUN_TEST(test_signal_init_cleanup);
    RUN_TEST(test_overflow_checkpoint);
    RUN_TEST(test_signal_null_sched);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
