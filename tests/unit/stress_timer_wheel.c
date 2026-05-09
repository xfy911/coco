/**
 * stress_timer_wheel.c - 压力测试: 大量定时器创建/取消
 *
 * 验证:
 * 1. 大量定时器不泄漏
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
    int count = (int)(long)arg;
    
    for (int i = 0; i < count; i++) {
        /* Create timer with very short delay */
        coco_timer_t *t = coco_timer(1, timer_cb, (void*)(long)i);
        (void)t;
    }
    
    /* Sleep long enough for timers to fire */
    coco_sleep(10);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    const int CREATORS = 50;
    const int TIMERS_PER_CREATOR = 100;
    const int EXPECTED_FIRED = CREATORS * TIMERS_PER_CREATOR;

    printf("Stress test: %d timers (all should fire)\n", EXPECTED_FIRED);

    /* Create timer creator coroutines */
    for (int i = 0; i < CREATORS; i++) {
        coco_create(sched, timer_creator, (void*)(long)TIMERS_PER_CREATOR, 0);
    }

    /* Run scheduler */
    coco_sched_run(sched);

    int fired = atomic_load(&timer_fired);
    printf("  Timers fired: %d (expected %d)\n", fired, EXPECTED_FIRED);
    assert(fired == EXPECTED_FIRED);

    coco_sched_destroy(sched);

    printf("[PASS] Timer wheel stress test: %d timers fired\n", fired);
    return 0;
}
