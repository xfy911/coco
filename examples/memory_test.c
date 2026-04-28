/**
 * memory_test.c - 内存占用和栈使用测试
 *
 * 用法:
 *   ./memory_test <count>              # 创建 count 个协程
 *   ./memory_test <count> --benchmark  # 测量分配性能
 *   ./memory_test <count> --telemetry  # 输出栈使用峰值
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define MAX_COROUTINES 100000

static int g_finished = 0;
static size_t g_total_stack_usage = 0;

/* 简单协程：递增计数器并退出 */
static void simple_coro(void *arg) {
    (void)arg;
    g_finished++;
    coco_yield();  /* 让遥测有机会采样 */
}

/* 深度调用协程：测试栈使用 */
static void deep_call(int depth) {
    char buffer[1024];  /* 使用一些栈空间 */
    memset(buffer, 0, sizeof(buffer));

    if (depth > 0) {
        deep_call(depth - 1);
    }
}

static void deep_coro(void *arg) {
    int depth = *(int*)arg;
    deep_call(depth);
    g_finished++;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <count> [--benchmark|--telemetry]\n", argv[0]);
        printf("  count: number of coroutines to create\n");
        printf("  --benchmark: measure allocation performance\n");
        printf("  --telemetry: output stack usage peak\n");
        return 1;
    }

    int count = atoi(argv[1]);
    if (count <= 0 || count > MAX_COROUTINES) {
        printf("Error: count must be 1-%d\n", MAX_COROUTINES);
        return 1;
    }

    bool benchmark = (argc > 2 && strcmp(argv[2], "--benchmark") == 0);
    bool telemetry = (argc > 2 && strcmp(argv[2], "--telemetry") == 0);

    printf("=== Memory Test ===\n");
    printf("Coroutines: %d\n", count);
    printf("Stack size: %d KB (default)\n", COCO_DEFAULT_STACK_SIZE / 1024);

    /* 创建调度器 */
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Error: Failed to create scheduler\n");
        return 1;
    }

    /* 记录开始时间（benchmark 模式） */
    struct timespec start, end;
    if (benchmark) {
        clock_gettime(CLOCK_MONOTONIC, &start);
    }

    /* 创建协程 */
    coco_coro_t *coros[MAX_COROUTINES];
    for (int i = 0; i < count; i++) {
        if (telemetry && i % 10 == 0) {
            /* 每 10 个协程使用深度调用测试栈使用 */
            static int depth = 5;
            coros[i] = coco_create(sched, (void(*)(void*))deep_coro, &depth, 0);
        } else {
            coros[i] = coco_create(sched, simple_coro, NULL, 0);
        }
        if (!coros[i]) {
            printf("Error: Failed to create coroutine %d\n", i);
            break;
        }
    }

    /* 记录创建完成时间（benchmark 模式） */
    if (benchmark) {
        clock_gettime(CLOCK_MONOTONIC, &end);
        long long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000LL
                             + (end.tv_nsec - start.tv_nsec) / 1000LL;
        printf("\nCreation time: %lld us\n", elapsed_us);
        printf("Per coroutine: %.2f us\n", (double)elapsed_us / count);
    }

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 输出遥测结果 */
    if (telemetry) {
        printf("\n=== Stack Usage Telemetry ===\n");
        for (int i = 0; i < count && i < 20; i++) {  /* 只显示前 20 个 */
            size_t usage = coco_get_stack_usage(coros[i]);
            g_total_stack_usage += usage;
            printf("  Coroutine %d: %zu bytes (%.1f KB)\n",
                   i, usage, usage / 1024.0);
        }
        if (count > 20) {
            printf("  ... (%d more coroutines)\n", count - 20);
        }
        printf("\nAverage stack usage: %.1f KB\n",
               (double)g_total_stack_usage / count / 1024.0);
    }

    printf("\nFinished: %d/%d coroutines\n", g_finished, count);

    /* 清理 */
    coco_sched_destroy(sched);

    printf("\n✅ Test completed\n");
    return 0;
}
