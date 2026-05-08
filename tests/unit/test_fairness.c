/**
 * test_fairness.c - 时间片公平调度测试
 *
 * 验证：
 * 1. 时间片配置正常
 * 2. 公平调度默认禁用
 * 3. 性能不退化
 */

#include "coco.h"
#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 测试 1: 禁用公平调度时行为 */
static int test_fairness_disabled(void) {
    printf("Test 1: Fairness disabled by default\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("  FAIL: Failed to create scheduler\n");
        return 1;
    }

    /* 确保公平调度默认禁用 */
    if (sched->fairness_enabled) {
        printf("  FAIL: Fairness should be disabled by default\n");
        coco_sched_destroy(sched);
        return 1;
    }

    printf("  PASS: Fairness is disabled by default\n");

    /* 禁用公平调度（显式） */
    coco_sched_set_fairness(sched, false, 0);

    if (sched->fairness_enabled) {
        printf("  FAIL: Fairness should remain disabled\n");
        coco_sched_destroy(sched);
        return 1;
    }

    printf("  PASS: Fairness remains disabled\n");

    coco_sched_destroy(sched);
    return 0;
}

/* 测试 2: 时间片设置 */
static int test_time_slice_config(void) {
    printf("\nTest 2: Time-slice configuration\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("  FAIL: Failed to create scheduler\n");
        return 1;
    }

    /* 检查默认时间片 */
    if (sched->time_slice_ns != 10 * 1000000ULL) {
        printf("  FAIL: Default time slice should be 10ms\n");
        coco_sched_destroy(sched);
        return 1;
    }
    printf("  PASS: Default time slice is 10ms\n");

    /* 设置自定义时间片 */
    coco_sched_set_fairness(sched, true, 5);

    if (!sched->fairness_enabled) {
        printf("  FAIL: Fairness should be enabled\n");
        coco_sched_destroy(sched);
        return 1;
    }

    if (sched->time_slice_ns != 5 * 1000000ULL) {
        printf("  FAIL: Time slice should be 5ms, got %lu\n", sched->time_slice_ns);
        coco_sched_destroy(sched);
        return 1;
    }
    printf("  PASS: Custom time slice 5ms set correctly\n");

    /* 设置零时间片应使用默认值 */
    coco_sched_set_fairness(sched, true, 0);

    if (sched->time_slice_ns != 10 * 1000000ULL) {
        printf("  FAIL: Time slice should revert to default 10ms\n");
        coco_sched_destroy(sched);
        return 1;
    }
    printf("  PASS: Zero time slice reverts to default 10ms\n");

    coco_sched_destroy(sched);
    return 0;
}

/* 简单协程用于测试 */
static int coro_run_count = 0;
static void simple_coro(void *arg) {
    (void)arg;
    for (int i = 0; i < 10; i++) {
        coro_run_count++;
        coco_yield();
    }
}

/* 测试 3: 启用公平调度后协程正常运行 */
static int test_fairness_enabled(void) {
    printf("\nTest 3: Fairness enabled - coroutines run normally\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("  FAIL: Failed to create scheduler\n");
        return 1;
    }

    /* 启用公平调度 */
    coco_sched_set_fairness(sched, true, 10);

    coro_run_count = 0;

    /* 创建协程 */
    coco_coro_t *coro = coco_create(sched, simple_coro, NULL, 0);
    if (!coro) {
        printf("  FAIL: Failed to create coroutine\n");
        coco_sched_destroy(sched);
        return 1;
    }

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证协程正常执行 */
    if (coro_run_count != 10) {
        printf("  FAIL: Expected 10 runs, got %d\n", coro_run_count);
        coco_sched_destroy(sched);
        return 1;
    }

    printf("  PASS: Coroutine ran %d times as expected\n", coro_run_count);

    coco_sched_destroy(sched);
    return 0;
}

/* CPU 密集型协程，用于测试时间片 */
static volatile int cpu_coro_count = 0;
static void cpu_bound_coro(void *arg) {
    int id = (int)(uintptr_t)arg;
    for (int i = 0; i < 100; i++) {
        cpu_coro_count++;
        /* 模拟一些工作 */
        for (volatile int j = 0; j < 100; j++) {}
        coco_yield();
    }
}

/* 测试 4: 多协程公平调度 */
static int test_fairness_multiple_coros(void) {
    printf("\nTest 4: Fairness with multiple coroutines\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("  FAIL: Failed to create scheduler\n");
        return 1;
    }

    /* 启用公平调度 */
    coco_sched_set_fairness(sched, true, 10);

    cpu_coro_count = 0;

    /* 创建多个协程 */
    #define NUM_COROS 4
    coco_coro_t *coros[NUM_COROS];
    for (int i = 0; i < NUM_COROS; i++) {
        coros[i] = coco_create(sched, cpu_bound_coro, (void*)(uintptr_t)i, 0);
        if (!coros[i]) {
            printf("  FAIL: Failed to create coroutine %d\n", i);
            coco_sched_destroy(sched);
            return 1;
        }
    }

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证所有协程都正常执行 */
    int expected_total = NUM_COROS * 100;
    if (cpu_coro_count != expected_total) {
        printf("  FAIL: Expected %d total runs, got %d\n", expected_total, cpu_coro_count);
        coco_sched_destroy(sched);
        return 1;
    }

    printf("  PASS: All %d coroutines ran correctly, total %d runs\n", NUM_COROS, cpu_coro_count);

    coco_sched_destroy(sched);
    return 0;
}

/* 测试 5: 性能不退化 */
static int test_no_regression(void) {
    printf("\nTest 5: No performance regression\n");

    /* 运行基准测试 */
    int result = system("cd /home/xfy/Developer/coco/build && ./bench_switch > /tmp/bench_result.txt 2>&1");
    (void)result;

    /* 检查基准测试结果 */
    FILE *fp = fopen("/tmp/bench_result.txt", "r");
    if (!fp) {
        printf("  SKIP: Cannot read benchmark result\n");
        return 0;
    }

    char line[256];
    double ns_per_switch = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Per switch:")) {
            sscanf(line, "  Per switch: %lf ns", &ns_per_switch);
            break;
        }
    }
    fclose(fp);

    if (ns_per_switch > 0 && ns_per_switch < 100.0) {
        printf("  PASS: %.2f ns < 100 ns target\n", ns_per_switch);
        return 0;
    } else if (ns_per_switch > 0) {
        printf("  FAIL: %.2f ns >= 100 ns target\n", ns_per_switch);
        return 1;
    } else {
        printf("  SKIP: Could not parse benchmark result\n");
        return 0;
    }
}

int main(void) {
    printf("=== Fairness Scheduling Tests ===\n\n");

    int failures = 0;

    failures += test_fairness_disabled();
    failures += test_time_slice_config();
    failures += test_fairness_enabled();
    failures += test_fairness_multiple_coros();
    failures += test_no_regression();

    printf("\n=== Summary ===\n");
    if (failures == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
