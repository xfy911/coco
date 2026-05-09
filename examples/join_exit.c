/**
 * join_exit.c - Join 与 Exit 示例
 *
 * 展示 coco_join 等待协程结果、coco_exit 返回值，
 * 实现协程间的结果传递。
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* 计算斐波那契数列，通过 exit 返回结果 */
void fibonacci_coro(void *arg) {
    int n = *(int *)arg;
    printf("Fibonacci coroutine: computing fib(%d)\n", n);

    if (n <= 1) {
        coco_exit(coco_self(), (void *)(intptr_t)n);
    }

    long long a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        long long tmp = a + b;
        a = b;
        b = tmp;
        /* 每次迭代让出 CPU，模拟耗时计算 */
        coco_yield();
    }

    printf("Fibonacci coroutine: fib(%d) = %lld\n", n, b);
    coco_exit(coco_self(), (void *)(intptr_t)b);
}

/* Joiner 协程：创建子协程并等待结果 */
void joiner_coro(void *arg) {
    (void)arg;
    int n = 10;

    coco_sched_t *sched = coco_sched_get_current();
    coco_coro_t *fib = coco_create(sched, fibonacci_coro, &n, 0);

    printf("Joiner: waiting for fibonacci(%d)...\n", n);

    void *result = coco_join(fib);

    printf("Joiner: got result fib(%d) = %lld\n", n, (long long)(intptr_t)result);

    /* 并发等待多个协程 */
    int ns[] = {5, 8, 13};
    coco_coro_t *coros[3];
    for (int i = 0; i < 3; i++) {
        coros[i] = coco_create(sched, fibonacci_coro, &ns[i], 0);
    }

    for (int i = 0; i < 3; i++) {
        void *res = coco_join(coros[i]);
        printf("Joiner: fib(%d) = %lld\n", ns[i], (long long)(intptr_t)res);
    }
}

int main(void) {
    printf("=== Join & Exit Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    coco_create(sched, joiner_coro, NULL, 0);

    coco_sched_run(sched);

    coco_sched_destroy(sched);

    printf("\n✅ Join & Exit example completed\n");
    return 0;
}
