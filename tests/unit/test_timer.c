/**
 * test_timer.c - 定时器单元测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

static int timer_count = 0;

void timer_callback(void *arg) {
    (void)arg;
    timer_count++;
    printf("Timer fired! count=%d\n", timer_count);
}

void test_timer_create(void) {
    printf("test_timer_create: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_timer_t *timer = coco_timer(10, timer_callback, NULL);
    assert(timer != NULL);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_cancel(void) {
    printf("test_timer_cancel: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_timer_t *timer = coco_timer(100, timer_callback, NULL);
    assert(timer != NULL);

    coco_timer_cancel(timer);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_accuracy(void) {
    printf("test_timer_accuracy: ");

    timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    /* 使用调度器的时间轮 */
    coco_timer(5, timer_callback, NULL);
    coco_timer(5, timer_callback, NULL);
    coco_timer(5, timer_callback, NULL);

    /* 模拟时间流逝 */
    usleep(10000);  /* 等待 10ms */

    /* 处理 tick（使用调度器的时间轮） */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("timer_count=%d (expected >= 3)\n", timer_count);
    assert(timer_count >= 3);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

int main(void) {
    printf("=== Timer Tests ===\n");
    test_timer_create();
    test_timer_cancel();
    test_timer_accuracy();
    printf("All tests passed!\n");
    return 0;
}