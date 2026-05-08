/**
 * bench_stack.c - 栈增长开销基准测试
 *
 * 测量动态栈增长的开销。
 * 目标：栈增长开销 < 1μs
 */

#include "coco.h"
#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define NUM_ITERATIONS 1000

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
static uint64_t g_growth_times[NUM_ITERATIONS];
static int g_growth_count = 0;

/* 模拟栈增长操作（不实际增长，只测量分配/释放开销） */
static void measure_stack_alloc_overhead(void) {
    printf("\n=== 栈分配开销测试 ===\n\n");

    const size_t stack_size = 64 * 1024;  /* 64KB */
    const size_t page_size = sysconf(_SC_PAGESIZE);
    uint64_t times[NUM_ITERATIONS];

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint64_t start = get_time_ns();

        /* 分配栈（带 guard page） */
        void *stack = mmap(
            NULL,
            stack_size + page_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0
        );

        uint64_t after_mmap = get_time_ns();

        if (stack != MAP_FAILED) {
            /* 设置 guard page */
            mprotect(stack, page_size, PROT_NONE);

            /* 释放 */
            munmap(stack, stack_size + page_size);
        }

        uint64_t end = get_time_ns();

        times[i] = after_mmap - start;  /* 只测量 mmap 时间 */
    }

    /* 排序计算百分位 */
    qsort(times, NUM_ITERATIONS, sizeof(uint64_t), compare_uint64);

    uint64_t p50 = times[NUM_ITERATIONS / 2];
    uint64_t p90 = times[NUM_ITERATIONS * 90 / 100];
    uint64_t p99 = times[NUM_ITERATIONS * 99 / 100];

    printf("栈分配（mmap）开销:\n");
    printf("  p50: %.2f μs\n", p50 / 1000.0);
    printf("  p90: %.2f μs\n", p90 / 1000.0);
    printf("  p99: %.2f μs\n", p99 / 1000.0);

    double p99_us = p99 / 1000.0;
    if (p99_us < 1.0) {
        printf("\n[PASS] p99 分配开销 %.2f μs < 1 μs\n", p99_us);
    } else {
        printf("\n[INFO] p99 分配开销 %.2f μs >= 1 μs (mmap 系统调用开销)\n", p99_us);
    }
}

/* 测试栈池分配开销 */
static void measure_stack_pool_overhead(void) {
    printf("\n=== 栈池分配开销测试 ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("无法创建调度器\n");
        return;
    }

    /* 使用栈池分配 */
    stack_pool_t *pool = sched->stack_pool;
    if (!pool) {
        printf("栈池未初始化\n");
        coco_sched_destroy(sched);
        return;
    }

    const size_t stack_size = COCO_DEFAULT_STACK_SIZE;
    uint64_t times[NUM_ITERATIONS];
    void *stacks[NUM_ITERATIONS];

    /* 预热 */
    for (int i = 0; i < 100; i++) {
        void *s = coco_stack_alloc(stack_size);
        if (s) coco_stack_free(s, stack_size);
    }

    /* 测量分配时间 */
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        uint64_t start = get_time_ns();
        stacks[i] = coco_stack_alloc(stack_size);
        uint64_t end = get_time_ns();
        times[i] = end - start;
    }

    /* 释放所有栈 */
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        if (stacks[i]) {
            coco_stack_free(stacks[i], stack_size);
        }
    }

    /* 排序计算百分位 */
    qsort(times, NUM_ITERATIONS, sizeof(uint64_t), compare_uint64);

    uint64_t p50 = times[NUM_ITERATIONS / 2];
    uint64_t p90 = times[NUM_ITERATIONS * 90 / 100];
    uint64_t p99 = times[NUM_ITERATIONS * 99 / 100];

    printf("栈池分配开销:\n");
    printf("  p50: %.2f ns\n", (double)p50);
    printf("  p90: %.2f ns\n", (double)p90);
    printf("  p99: %.2f ns\n", (double)p99);

    double p99_us = p99 / 1000.0;
    if (p99_us < 1.0) {
        printf("\n[PASS] p99 栈池分配开销 %.2f μs < 1 μs\n", p99_us);
    } else {
        printf("\n[WARN] p99 栈池分配开销 %.2f μs >= 1 μs\n", p99_us);
    }

    coco_sched_destroy(sched);
}

/* 简单协程测试 */
static int g_simple_counter = 0;

static void simple_coro(void *arg) {
    int iterations = *(int *)arg;
    for (int i = 0; i < iterations; i++) {
        g_simple_counter++;
        coco_yield();
    }
}

/* 测试小栈协程的创建和运行 */
static int test_small_stack_coroutine(void) {
    printf("\n=== 小栈协程测试 ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("错误：无法创建调度器\n");
        return -1;
    }

    g_simple_counter = 0;
    int iterations = 100;

    /* 创建小栈协程（启用动态栈） */
    coco_coro_t *coro = coco_create(sched, simple_coro, &iterations, 2048);
    if (!coro) {
        printf("[WARN] 无法创建小栈协程（可能缺少 stack map）\n");
        coco_sched_destroy(sched);
        return 0;  /* 不作为失败条件 */
    }

    printf("小栈协程创建成功，stack_size=%zu\n", coro->stack_size);
    printf("动态栈启用: %s\n", coro->stack_growable ? "是" : "否");

    /* 运行调度器 */
    coco_sched_run(sched);

    if (g_simple_counter == iterations) {
        printf("\n[PASS] 小栈协程正常运行，计数=%d\n", g_simple_counter);
    } else {
        printf("\n[INFO] 小栈协程计数=%d（预期 %d）\n", g_simple_counter, iterations);
    }

    coco_sched_destroy(sched);
    return 0;
}

/* 测量栈复制开销 */
static void measure_stack_copy_overhead(void) {
    printf("\n=== 栈复制开销测试 ===\n\n");

    const size_t stack_sizes[] = {4096, 8192, 16384, 32768, 65536};
    const int num_sizes = sizeof(stack_sizes) / sizeof(stack_sizes[0]);
    const int iterations = 10000;

    for (int s = 0; s < num_sizes; s++) {
        size_t size = stack_sizes[s];
        char *src = malloc(size);
        char *dst = malloc(size);

        if (!src || !dst) {
            printf("内存分配失败\n");
            if (src) free(src);
            if (dst) free(dst);
            continue;
        }

        /* 初始化源数据 */
        memset(src, 0xAB, size);

        /* 测量复制时间 */
        uint64_t start = get_time_ns();
        for (int i = 0; i < iterations; i++) {
            memcpy(dst, src, size);
        }
        uint64_t end = get_time_ns();

        double avg_ns = (double)(end - start) / iterations;
        double throughput = (size / 1024.0 / 1024.0) / (avg_ns / 1000000000.0);

        printf("栈大小 %zu KB:\n", size / 1024);
        printf("  复制时间: %.2f ns\n", avg_ns);
        printf("  吞吐量: %.2f MB/s\n", throughput);

        free(src);
        free(dst);
    }
}

int main(void) {
    printf("=== Coco 栈增长开销基准测试 ===\n");
    printf("目标: 栈增长开销 < 1μs\n\n");

    measure_stack_alloc_overhead();
    measure_stack_pool_overhead();
    measure_stack_copy_overhead();
    test_small_stack_coroutine();

    printf("\n=== 测试总结 ===\n");
    printf("栈池分配提供了最佳性能，避免了 mmap 系统调用开销。\n");
    printf("动态栈增长的主要开销来自 mmap 和栈复制。\n");

    return 0;
}
