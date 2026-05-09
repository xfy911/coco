/**
 * multithread.c - 多线程调度器示例
 *
 * 展示 coco_global_sched_start/stop、coco_go 在多线程环境下
 * 创建和运行协程，以及 coco_go_on 绑定特定 P 执行。
 */

#include "coco.h"
#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>

static atomic_int g_total = 0;

/* 被 coco_go 启动的协程 */
void worker_coro(void *arg) {
    int id = (int)(intptr_t)arg;
    printf("[P?] Worker %d: running on thread\n", id);

    for (int i = 0; i < 3; i++) {
        atomic_fetch_add(&g_total, 1);
        printf("[P?] Worker %d: step %d, total = %d\n", id, i, atomic_load(&g_total));
        coco_yield();
    }

    printf("[P?] Worker %d: done\n", id);
}

/* 绑定到特定 P 的协程 */
void pinned_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    printf("[P0] Pinned worker %d: running on P 0\n", id);

    for (int i = 0; i < 3; i++) {
        atomic_fetch_add(&g_total, 1);
        printf("[P0] Pinned worker %d: step %d, total = %d\n", id, i, atomic_load(&g_total));
        coco_yield();
    }

    printf("[P0] Pinned worker %d: done\n", id);
}

int main(void) {
    printf("=== Multi-threaded Scheduler Example ===\n\n");

    int num_workers = 2;
    printf("Starting global scheduler with %d worker threads\n\n", num_workers);

    if (coco_global_sched_start(num_workers) != COCO_OK) {
        printf("Failed to start global scheduler\n");
        return 1;
    }

    /* 使用 coco_go 启动协程（自动分配 P） */
    for (int i = 1; i <= 4; i++) {
        coco_go(worker_coro, (void *)(intptr_t)i);
    }

    /* 使用 coco_go_on 绑定到 P 0 */
    for (int i = 5; i <= 6; i++) {
        coco_go_on(0, pinned_worker, (void *)(intptr_t)i);
    }

    /* 使用 coco_go_with_opts 自定义参数 */
    coco_go_opts_t opts = {
        .stack_size = COCO_STACK_SMALL,
        .context = NULL,
        .priority = COCO_PRIORITY_HIGH,
        .p_id = -1
    };
    coco_go_with_opts(worker_coro, (void *)(intptr_t)7, &opts);
    printf("Launched worker 7 with HIGH priority and 16KB stack\n\n");

    /* 等待所有协程完成 */
    coco_global_sched_wait();
    printf("\nAll coroutines finished, total work units = %d\n", atomic_load(&g_total));

    coco_global_sched_stop();

    printf("\n✅ Multi-threaded scheduler example completed\n");
    return 0;
}
