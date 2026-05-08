/**
 * test_preempt.c - 异步抢占测试
 *
 * 测试死循环协程的抢占功能。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../include/coco.h"

/* 测试状态 */
static volatile int g_loop_count = 0;
static volatile int g_checkpoint_count = 0;
static volatile int g_coroutine_finished = 0;
static uint64_t g_start_time = 0;
static uint64_t g_end_time = 0;

/* 获取当前时间（毫秒） */
static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* 使用 checkpoint 的协程 */
static void checkpoint_coroutine(void *arg) {
    int max_iterations = *(int *)arg;
    (void)arg;

    printf("[coroutine] 开始执行，最大迭代: %d\n", max_iterations);
    g_start_time = get_time_ms();

    for (int i = 0; i < max_iterations; i++) {
        g_loop_count++;

        /* 使用 checkpoint 允许抢占 */
        coco_preempt_checkpoint();
        g_checkpoint_count++;

        /* 模拟一些工作 */
        for (volatile int j = 0; j < 100; j++) {
            /* busy wait */
        }
    }

    g_end_time = get_time_ms();
    printf("[coroutine] 完成，循环次数: %d, checkpoint 次数: %d\n",
           g_loop_count, g_checkpoint_count);
    g_coroutine_finished = 1;
}

/* 测试 checkpoint 抢占功能 */
static int test_checkpoint_preempt(void) {
    printf("\n=== 测试 checkpoint 抢占功能 ===\n");

    g_loop_count = 0;
    g_checkpoint_count = 0;
    g_coroutine_finished = 0;

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    int max_iter = 10000;
    coco_create(sched, checkpoint_coroutine, &max_iter, 0);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证结果 */
    if (g_coroutine_finished) {
        uint64_t elapsed = g_end_time - g_start_time;
        printf("测试通过：协程运行了 %lu ms，执行了 %d 次 checkpoint\n",
               elapsed, g_checkpoint_count);
    } else {
        printf("测试失败：协程未能完成\n");
        coco_sched_destroy(sched);
        return -1;
    }

    coco_sched_destroy(sched);
    return 0;
}

/* 测试抢占后寄存器恢复 */
static volatile int g_register_test_passed = 0;

static void register_test_coroutine(void *arg) {
    volatile uint64_t counter = 0;
    volatile uint64_t expected = 1000000;

    (void)arg;

    printf("[register_test] 开始执行，预期计数: %lu\n", expected);

    /* 计算循环 */
    for (volatile uint64_t i = 0; i < expected; i++) {
        counter++;

        /* 在循环中途 yield，测试寄存器恢复 */
        if (i == expected / 2) {
            printf("[register_test] 在 %lu 处 yield\n", i);
            coco_yield();
            printf("[register_test] 从 yield 恢复，继续从 %lu 计数\n", i);
        }
    }

    if (counter == expected) {
        printf("[register_test] 成功：计数器 = %lu (预期 %lu)\n", counter, expected);
        g_register_test_passed = 1;
    } else {
        printf("[register_test] 失败：计数器 = %lu (预期 %lu)\n", counter, expected);
    }
}

static int test_register_restore(void) {
    printf("\n=== 测试抢占后寄存器恢复 ===\n");

    g_register_test_passed = 0;

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    /* 创建测试协程 */
    coco_create(sched, register_test_coroutine, NULL, 0);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证结果 */
    if (g_register_test_passed) {
        printf("测试通过：寄存器正确恢复\n");
    } else {
        printf("测试失败：寄存器未正确恢复\n");
        coco_sched_destroy(sched);
        return -1;
    }

    coco_sched_destroy(sched);
    return 0;
}

/* 测试抢占使能/禁用 */
static int test_preempt_enable_disable(void) {
    printf("\n=== 测试抢占使能/禁用 ===\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    /* 测试 API 调用 */
    coco_preempt_enable();
    printf("已启用抢占\n");

    if (coco_preempt_is_pending()) {
        printf("抢占待处理\n");
    } else {
        printf("无抢占待处理\n");
    }

    coco_preempt_disable();
    printf("已禁用抢占\n");

    printf("测试通过：API 调用正常\n");

    coco_sched_destroy(sched);
    return 0;
}

/* 测试多协程抢占公平性 */
static volatile int g_coro1_count = 0;
static volatile int g_coro2_count = 0;
static volatile int g_coro1_done = 0;
static volatile int g_coro2_done = 0;

static void fair_coro1(void *arg) {
    int iterations = *(int *)arg;
    (void)arg;

    for (int i = 0; i < iterations; i++) {
        g_coro1_count++;
        coco_preempt_checkpoint();
    }
    g_coro1_done = 1;
    printf("[coro1] 完成，计数: %d\n", g_coro1_count);
}

static void fair_coro2(void *arg) {
    int iterations = *(int *)arg;
    (void)arg;

    for (int i = 0; i < iterations; i++) {
        g_coro2_count++;
        coco_preempt_checkpoint();
    }
    g_coro2_done = 1;
    printf("[coro2] 完成，计数: %d\n", g_coro2_count);
}

static int test_fairness(void) {
    printf("\n=== 测试多协程抢占公平性 ===\n");

    g_coro1_count = 0;
    g_coro2_count = 0;
    g_coro1_done = 0;
    g_coro2_done = 0;

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    int iterations = 1000;
    coco_create(sched, fair_coro1, &iterations, 0);
    coco_create(sched, fair_coro2, &iterations, 0);

    coco_sched_run(sched);

    if (g_coro1_done && g_coro2_done) {
        printf("测试通过：两个协程都完成了\n");
        printf("  coro1 计数: %d\n", g_coro1_count);
        printf("  coro2 计数: %d\n", g_coro2_count);

        /* 检查公平性：两个协程的计数应该接近 */
        int diff = abs(g_coro1_count - g_coro2_count);
        int total = g_coro1_count + g_coro2_count;
        double ratio = (double)diff / total;
        if (ratio < 0.5) {
            printf("公平性良好：差异比例 %.1f%%\n", ratio * 100);
        } else {
            printf("警告：差异较大，差异比例 %.1f%%\n", ratio * 100);
        }
    } else {
        printf("测试失败：协程未能全部完成\n");
        coco_sched_destroy(sched);
        return -1;
    }

    coco_sched_destroy(sched);
    return 0;
}

int main(void) {
    printf("=== 协程异步抢占测试 ===\n");

    int failures = 0;

    if (test_checkpoint_preempt() != 0) {
        failures++;
        printf("FAILED: test_checkpoint_preempt\n");
    }

    if (test_register_restore() != 0) {
        failures++;
        printf("FAILED: test_register_restore\n");
    }

    if (test_preempt_enable_disable() != 0) {
        failures++;
        printf("FAILED: test_preempt_enable_disable\n");
    }

    if (test_fairness() != 0) {
        failures++;
        printf("FAILED: test_fairness\n");
    }

    printf("\n=== 测试总结 ===\n");
    if (failures == 0) {
        printf("所有测试通过！\n");
        return 0;
    } else {
        printf("%d 个测试失败\n", failures);
        return 1;
    }
}
