/**
 * bench_switch.c - 上下文切换性能基准测试
 */

#include "coco.h"
#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SWITCH_COUNT 10000000  /* 10M 次切换 */
#define STACK_SIZE COCO_DEFAULT_STACK_SIZE

/* 全局变量 */
static coco_ctx_t main_ctx;
static coco_ctx_t coro_ctx;
static volatile int counter = 0;

/* 协程入口函数 */
static void coro_entry(void *arg) {
    (void)arg;
    while (1) {
        counter++;
        coco_ctx_switch(&coro_ctx, &main_ctx);
    }
}

/* 分配栈 (简单实现) */
static void *alloc_stack(size_t size) {
#ifdef __APPLE__
    void *stack = malloc(size);
    return stack + size;  /* 返回栈顶 */
#else
    void *stack = malloc(size);
    return stack + size;
#endif
}

int main(void) {
    printf("=== Context Switch Benchmark ===\n");
    printf("Target: < 100ns per switch\n");
    printf("Iterations: %d\n", SWITCH_COUNT);

    /* 分配栈 */
    void *stack_top = alloc_stack(STACK_SIZE);
    if (!stack_top) {
        printf("Error: Failed to allocate stack\n");
        return 1;
    }

    /* 初始化协程上下文 */
    coco_ctx_init(&coro_ctx, stack_top, coro_entry, NULL);

    /* 开始计时 */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 执行切换循环 */
    for (int i = 0; i < SWITCH_COUNT; i++) {
        coco_ctx_switch(&main_ctx, &coro_ctx);
    }

    /* 结束计时 */
    clock_gettime(CLOCK_MONOTONIC, &end);

    /* 计算耗时 */
    long long elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000LL
                          + (end.tv_nsec - start.tv_nsec);
    double ns_per_switch = (double)elapsed_ns / SWITCH_COUNT;

    printf("\nResults:\n");
    printf("  Total time: %.3f ms\n", elapsed_ns / 1000000.0);
    printf("  Switches: %d\n", SWITCH_COUNT);
    printf("  Per switch: %.2f ns\n", ns_per_switch);
    printf("  Counter verify: %d (expected %d)\n", counter, SWITCH_COUNT);

    /* 判断是否达标 */
    if (ns_per_switch < 100.0) {
        printf("\n✅ PASS: %.2f ns < 100 ns target\n", ns_per_switch);
    } else {
        printf("\n❌ FAIL: %.2f ns >= 100 ns target\n", ns_per_switch);
    }

    free(stack_top - STACK_SIZE);
    return ns_per_switch < 100.0 ? 0 : 1;
}