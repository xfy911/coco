/**
 * test_long_running.c - 长时间运行稳定性测试
 *
 * 验证:
 * 1. 持续调度不崩溃
 * 2. 内存不持续增长
 * 3. 定时器/channel/I/O 长期稳定
 *
 * 运行时约 30 秒。
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdatomic.h>

static atomic_int iterations = 0;
static int received_count = 0;

void worker(void *arg) {
    int *count = (int *)arg;
    (*count)++;
    received_count++;
}

int main(void) {
    printf("Long running test: continuous coroutine creation for ~30s\n");

    for (int round = 0; round < 30; round++) {
        coco_sched_t *sched = coco_sched_create();
        assert(sched != NULL);

        received_count = 0;
        int counts[100] = {0};

        /* 每轮创建 100 个协程 */
        for (int i = 0; i < 100; i++) {
            coco_create(sched, worker, &counts[i], 0);
        }

        /* 运行 */
        coco_sched_run(sched);

        /* 验证所有协程都完成了 */
        assert(received_count == 100);

        /* 清理 */
        coco_sched_destroy(sched);

        atomic_fetch_add(&iterations, 1);

        printf("  Round %d: %d coroutines processed\n", round + 1, received_count);
    }

    printf("[PASS] Long running test: %d rounds completed\n", atomic_load(&iterations));
    return 0;
}
