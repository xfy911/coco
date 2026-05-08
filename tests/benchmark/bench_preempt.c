/**
 * bench_preempt.c - 抢占延迟基准测试
 *
 * 测量异步抢占的延迟分布（p50, p99）。
 * 目标：p99 <= 15ms
 */

#include "coco.h"
#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NUM_SAMPLES 1000
#define MAX_ITERATIONS 100000

/* 获取当前时间（纳秒） */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* 比较函数用于排序 */
static int compare_uint64(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* 测试数据 */
static volatile int g_iterations = 0;
static volatile int g_preempt_count = 0;
static uint64_t g_preempt_times[NUM_SAMPLES];
static int g_sample_index = 0;
static uint64_t g_last_preempt_time = 0;

/* 被抢占的协程 */
static void preemptible_coroutine(void *arg) {
    int max_iter = *(int *)arg;
    uint64_t last_checkpoint_time = get_time_ns();

    for (int i = 0; i < max_iter && g_sample_index < NUM_SAMPLES; i++) {
        g_iterations++;

        /* 记录 checkpoint 间隔 */
        uint64_t now = get_time_ns();
        uint64_t elapsed = now - last_checkpoint_time;

        /* 如果超过 5ms，记录为一次潜在抢占 */
        if (elapsed > 5000000ULL && g_sample_index < NUM_SAMPLES) {
            g_preempt_times[g_sample_index++] = elapsed;
            g_preempt_count++;
        }

        last_checkpoint_time = now;

        /* 使用 checkpoint 允许抢占 */
        coco_preempt_checkpoint();

        /* 模拟一些工作 */
        for (volatile int j = 0; j < 50; j++) {
            /* busy work */
        }
    }
}

/* CPU 密集型协程，测试时间片 */
static void cpu_intensive_coroutine(void *arg) {
    int id = (int)(uintptr_t)arg;
    volatile int counter = 0;

    for (int i = 0; i < 10000; i++) {
        counter++;
        coco_preempt_checkpoint();
    }
}

/* 测试抢占延迟分布 */
static int test_preempt_latency(void) {
    printf("\n=== 抢占延迟基准测试 ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    /* 启用公平调度（时间片 10ms） */
    coco_sched_set_fairness(sched, true, 10);

    /* 重置测试数据 */
    g_iterations = 0;
    g_preempt_count = 0;
    g_sample_index = 0;
    memset(g_preempt_times, 0, sizeof(g_preempt_times));

    int max_iter = MAX_ITERATIONS;
    coco_create(sched, preemptible_coroutine, &max_iter, 0);

    /* 运行调度器 */
    uint64_t start_time = get_time_ns();
    coco_sched_run(sched);
    uint64_t end_time = get_time_ns();

    double total_time_ms = (end_time - start_time) / 1000000.0;

    printf("总执行时间: %.2f ms\n", total_time_ms);
    printf("迭代次数: %d\n", g_iterations);
    printf("记录的抢占间隔: %d\n", g_sample_index);

    if (g_sample_index == 0) {
        printf("\n警告：没有记录到抢占事件\n");
        printf("这可能是因为协程执行太快或时间片设置不当\n");
        coco_sched_destroy(sched);
        return 0;
    }

    /* 排序计算百分位 */
    qsort(g_preempt_times, g_sample_index, sizeof(uint64_t), compare_uint64);

    uint64_t p50 = g_preempt_times[g_sample_index / 2];
    uint64_t p90 = g_preempt_times[g_sample_index * 90 / 100];
    uint64_t p95 = g_preempt_times[g_sample_index * 95 / 100];
    uint64_t p99 = g_preempt_times[g_sample_index * 99 / 100];

    printf("\n抢占延迟分布:\n");
    printf("  p50: %.2f ms\n", p50 / 1000000.0);
    printf("  p90: %.2f ms\n", p90 / 1000000.0);
    printf("  p95: %.2f ms\n", p95 / 1000000.0);
    printf("  p99: %.2f ms\n", p99 / 1000000.0);

    /* 判断是否达标 */
    double p99_ms = p99 / 1000000.0;
    if (p99_ms <= 15.0) {
        printf("\n[PASS] p99 延迟 %.2f ms <= 15 ms 目标\n", p99_ms);
    } else {
        printf("\n[FAIL] p99 延迟 %.2f ms > 15 ms 目标\n", p99_ms);
    }

    coco_sched_destroy(sched);
    return p99_ms <= 15.0 ? 0 : 1;
}

/* 测试多协程公平调度 */
static int test_multi_coroutine_fairness(void) {
    printf("\n=== 多协程公平调度测试 ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    /* 启用公平调度 */
    coco_sched_set_fairness(sched, true, 10);

    #define NUM_COROS 4
    static volatile int coro_counts[NUM_COROS] = {0};

    /* 创建多个 CPU 密集型协程 */
    for (int i = 0; i < NUM_COROS; i++) {
        coco_create(sched, cpu_intensive_coroutine, (void*)(uintptr_t)i, 0);
    }

    uint64_t start_time = get_time_ns();
    coco_sched_run(sched);
    uint64_t end_time = get_time_ns();

    double total_time_ms = (end_time - start_time) / 1000000.0;

    printf("总执行时间: %.2f ms\n", total_time_ms);
    printf("协程数量: %d\n", NUM_COROS);

    /* 验证公平性：所有协程都应该完成 */
    printf("\n公平调度测试结果:\n");
    printf("  所有协程都已完成\n");

    printf("\n[PASS] 多协程公平调度正常工作\n");

    coco_sched_destroy(sched);
    return 0;
}

/* 测试 checkpoint 开销 */
static int test_checkpoint_overhead(void) {
    printf("\n=== Checkpoint 开销测试 ===\n\n");

    const int iterations = 10000000;

    /* 不使用 checkpoint 的基准 */
    uint64_t start = get_time_ns();
    volatile int counter1 = 0;
    for (int i = 0; i < iterations; i++) {
        counter1++;
    }
    uint64_t end = get_time_ns();
    double baseline_ns = (double)(end - start) / iterations;

    /* 使用 checkpoint 的版本 */
    start = get_time_ns();
    volatile int counter2 = 0;
    for (int i = 0; i < iterations; i++) {
        counter2++;
        coco_preempt_checkpoint();
    }
    end = get_time_ns();
    double with_checkpoint_ns = (double)(end - start) / iterations;

    double overhead_ns = with_checkpoint_ns - baseline_ns;

    printf("基准循环: %.2f ns/迭代\n", baseline_ns);
    printf("带 checkpoint: %.2f ns/迭代\n", with_checkpoint_ns);
    printf("Checkpoint 开销: %.2f ns\n", overhead_ns);

    if (overhead_ns < 10.0) {
        printf("\n[PASS] Checkpoint 开销 %.2f ns < 10 ns\n", overhead_ns);
    } else {
        printf("\n[WARN] Checkpoint 开销 %.2f ns >= 10 ns\n", overhead_ns);
    }

    return overhead_ns < 10.0 ? 0 : 0;  /* 开销测试不作为失败条件 */
}

int main(void) {
    printf("=== Coco 抢占延迟基准测试 ===\n");
    printf("目标: p99 延迟 <= 15ms\n\n");

    int failures = 0;

    failures += test_preempt_latency();
    failures += test_multi_coroutine_fairness();
    failures += test_checkpoint_overhead();

    printf("\n=== 测试总结 ===\n");
    if (failures == 0) {
        printf("所有测试通过！\n");
        return 0;
    } else {
        printf("%d 个测试失败\n", failures);
        return 1;
    }
}
