/**
 * cancel.c - 协程取消示例
 *
 * 展示 coco_cancel 和 coco_cancelled 的用法，
 * 演示如何优雅地取消正在运行的协程。
 */

#include "coco.h"
#include <stdio.h>
#include <stdint.h>

static coco_coro_t *g_worker;

/* 长时间运行的协程，定期检查取消状态 */
void long_task(void *arg) {
    int id = *(int *)arg;
    printf("Worker %d: started\n", id);

    for (int i = 0; i < 100; i++) {
        /* 检查是否已被取消 */
        if (coco_cancelled()) {
            printf("Worker %d: cancelled at iteration %d, cleaning up...\n", id, i);
            return;
        }

        printf("Worker %d: working... iteration %d\n", id, i);
        coco_sleep(10);
    }

    printf("Worker %d: completed all work\n", id);
}

/* 取消者协程：等待一段时间后取消 worker */
void canceller(void *arg) {
    (void)arg;
    printf("Canceller: waiting 50ms before cancel...\n");
    coco_sleep(50);
    printf("Canceller: cancelling worker now\n");
    coco_cancel(g_worker);
    printf("Canceller: cancel signal sent\n");
}

int main(void) {
    printf("=== Coroutine Cancellation Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    int id = 1;
    g_worker = coco_create(sched, long_task, &id, 0);
    coco_create(sched, canceller, NULL, 0);

    coco_sched_run(sched);

    printf("\nWorker state after cancel: %d\n", coco_get_state(g_worker));

    coco_sched_destroy(sched);

    printf("\n✅ Cancel example completed\n");
    return 0;
}
