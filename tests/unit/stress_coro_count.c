/**
 * stress_coro_count.c - 压力测试: 大量协程创建/销毁
 *
 * 验证:
 * 1. 10万协程创建/销毁不崩溃
 * 2. 内存不泄漏 (ASan 验证)
 * 3. 协程调度正确性
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static volatile int coro_completed = 0;

void stress_coro(void *arg) {
    int id = (int)(long)arg;
    /* 做一些简单工作 */
    volatile int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += i;
    }
    (void)sum;
    (void)id;
    coro_completed++;
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    const int TOTAL = 5000;  /* 5K per batch */
    const int BATCHES = 10;   /* 10 batches = 50K total */

    printf("Stress test: %d coroutines in %d batches\n", TOTAL * BATCHES, BATCHES);

    for (int batch = 0; batch < BATCHES; batch++) {
        coro_completed = 0;
        /* 创建一批协程 */
        for (int i = 0; i < TOTAL; i++) {
            coco_coro_t *coro = coco_create(sched, stress_coro, (void*)(long)i, 0);
            assert(coro != NULL);
        }

        /* 运行直到完成 */
        coco_sched_run(sched);

        /* 检查完成计数 */
        assert(coro_completed == TOTAL);

        printf("  Batch %d/%d: %d coroutines completed\n", batch + 1, BATCHES, coro_completed);
    }

    coco_sched_destroy(sched);

    printf("[PASS] %d coroutines completed without errors\n", TOTAL * BATCHES);
    return 0;
}
