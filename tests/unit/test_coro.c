/**
 * test_coro.c - 协程生命周期单元测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>

#define STACK_SIZE (64 * 1024)

static int counter = 0;

void coro_func(void *arg) {
    int *p = (int*)arg;
    for (int i = 0; i < 5; i++) {
        counter++;
        (*p)++;
        printf("Coroutine iteration %d, counter=%d\n", i, counter);
        coco_yield();
    }
    printf("Coroutine finished\n");
}

void test_coro_create(void) {
    printf("test_coro_create: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    int arg = 0;
    coco_coro_t *coro = coco_create(sched, coro_func, &arg, STACK_SIZE);
    assert(coro != NULL);
    assert(coco_get_state(coro) == COCO_STATE_READY);
    assert(coco_get_id(coro) > 0);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_coro_yield(void) {
    printf("test_coro_yield: ");

    counter = 0;
    coco_sched_t *sched = coco_sched_create();
    int arg = 0;

    coco_create(sched, coro_func, &arg, STACK_SIZE);

    /* 运行调度器 */
    coco_sched_run(sched);

    printf("Final counter=%d, arg=%d\n", counter, arg);
    assert(counter == 5);
    assert(arg == 5);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_multiple_coros(void) {
    printf("test_multiple_coros: ");

    counter = 0;
    coco_sched_t *sched = coco_sched_create();

    int arg1 = 0, arg2 = 0, arg3 = 0;
    coco_create(sched, coro_func, &arg1, STACK_SIZE);
    coco_create(sched, coro_func, &arg2, STACK_SIZE);
    coco_create(sched, coro_func, &arg3, STACK_SIZE);

    coco_sched_run(sched);

    printf("Final counter=%d (expected 15)\n", counter);
    assert(counter == 15);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

int main(void) {
    printf("=== Coroutine Tests ===\n");
    test_coro_create();
    test_coro_yield();
    test_multiple_coros();
    printf("All tests passed!\n");
    return 0;
}