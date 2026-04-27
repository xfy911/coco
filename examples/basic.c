/**
 * basic.c - 基础用法示例
 *
 * 展示协程创建、yield、join 等基本操作。
 */

#include "coco.h"
#include <stdio.h>

static int global_counter = 0;

void coro_func(void *arg) {
    int id = *(int*)arg;
    printf("Coroutine %d started, counter = %d\n", id, global_counter);

    for (int i = 0; i < 3; i++) {
        global_counter++;
        printf("Coroutine %d iteration %d, counter = %d\n", id, i, global_counter);
        coco_yield();
    }

    printf("Coroutine %d finished\n", id);
}

int main(void) {
    printf("=== Basic Coroutine Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    /* 创建两个协程 */
    int id1 = 1, id2 = 2;
    coco_coro_t *coro1 = coco_create(sched, coro_func, &id1, 0);
    coco_coro_t *coro2 = coco_create(sched, coro_func, &id2, 0);

    printf("Created coroutines: id1=%llu, id2=%llu\n\n",
           (unsigned long long)coco_get_id(coro1), (unsigned long long)coco_get_id(coro2));

    /* 运行调度器 */
    coco_sched_run(sched);

    printf("\nFinal counter = %d\n", global_counter);

    coco_sched_destroy(sched);
    return 0;
}