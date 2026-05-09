/**
 * sched_run_once.c - 逐步调度控制示例
 *
 * 展示 coco_sched_run_once 实现逐步调度，
 * 适合需要精细控制调度节奏的场景（如游戏主循环、
 * 仿真步进）。
 */

#include "coco.h"
#include <stdio.h>

static int g_step = 0;

void step_coro_a(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        g_step++;
        printf("  [step %d] Coro A: iteration %d\n", g_step, i);
        coco_yield();
    }
    printf("  Coro A: done\n");
}

void step_coro_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        g_step++;
        printf("  [step %d] Coro B: iteration %d\n", g_step, i);
        coco_yield();
    }
    printf("  Coro B: done\n");
}

int main(void) {
    printf("=== Step-by-step Scheduler Control Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    coco_create(sched, step_coro_a, NULL, 0);
    coco_create(sched, step_coro_b, NULL, 0);

    printf("Running scheduler step by step...\n");
    printf("(Each run_once dispatches one ready coroutine)\n\n");

    int iterations = 0;
    while (coco_sched_run_once(sched) == COCO_OK) {
        iterations++;
        printf("  => iteration %d complete\n\n", iterations);
    }

    printf("Scheduler finished after %d iterations, total coroutine steps = %d\n",
           iterations, g_step);

    coco_sched_destroy(sched);

    printf("\n✅ Step-by-step scheduler example completed\n");
    return 0;
}
