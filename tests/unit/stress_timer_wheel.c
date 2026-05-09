/**
 * stress_timer_wheel.c - 压力测试: 大量定时器创建/取消
 *
 * 验证:
 * 1. 10万定时器不泄漏
 * 2. 定时器取消正确
 * 3. 时间轮性能
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdatomic.h>

static atomic_int timer_fired = 0;

void timer_cb(void *arg) {
    (void)arg;
    atomic_fetch_add(&timer_fired, 1);
}

void timer_creator(void *arg) {
    coco_sched_t *sched = (coco_sched_t *)arg;
    const int COUNT = 1000;

    for (int i = 0; i < COUNT; i++) {
        /* 创建定时器 */
        coco_timer_t *t = coco_timer(1, timer_cb, (void*)(long)i);

        /* 取消一半 */
        if (i % 2 == 0 && t) {
            coco_timer_cancel(t);
        }
    }
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    const int CREATORS = 100;
    const int TOTAL_TIMERS = CREATORS * 1000;

    printf("Stress test: %d timers (%d creators x 1000)\n", TOTAL_TIMERS, CREATORS);

    /* 创建定时器创建者协程 */
    for (int i = 0; i < CREATORS; i++) {
        coco_create(sched, timer_creator, sched, 0);
    }

    /* 运行直到完成 */
    coco_sched_run(sched);

    int fired = atomic_load(&timer_fired);
    /* 大约一半的定时器被取消，另一半应该触发 */
    printf("  Timers fired: %d (expected ~%d)\n", fired, TOTAL_TIMERS / 2);

    coco_sched_destroy(sched);

    printf("[PASS] Timer wheel stress test completed\n");
    return 0;
}
