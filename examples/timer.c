/**
 * timer.c - Timer example
 *
 * Demonstrates timer creation, cancellation, and coco_sleep usage.
 */

#include "coco.h"
#include <stdio.h>
#include <stdint.h>

static int g_timer_count = 0;

/* Timer callback - runs in scheduler context */
void on_timer(void *arg) {
    int id = (int)(intptr_t)arg;
    printf("Timer %d fired, total = %d\n", id, ++g_timer_count);
}

/* Coroutine that uses coco_sleep */
void sleep_coro(void *arg) {
    int id = *(int*)arg;
    printf("Coroutine %d: sleeping 100ms...\n", id);
    coco_sleep(100);
    printf("Coroutine %d: woke up\n", id);
}

/* Coroutine that waits for all timers */
void waiter_coro(void *arg) {
    (void)arg;
    printf("Waiter: sleeping 200ms to let all timers fire...\n");
    coco_sleep(200);
    printf("Waiter: done\n");
}

int main(void) {
    printf("=== Timer Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    /* Create timers that fire independently */
    coco_timer(50, on_timer, (void*)(intptr_t)1);
    coco_timer(100, on_timer, (void*)(intptr_t)2);
    coco_timer(150, on_timer, (void*)(intptr_t)3);

    /* Create a timer to cancel */
    coco_timer_t *cancel_timer = coco_timer(200, on_timer, (void*)(intptr_t)99);
    printf("Created 4 timers, cancelling timer 99...\n");
    coco_timer_cancel(cancel_timer);
    printf("Timer 99 cancelled\n\n");

    /* Create coroutines that use coco_sleep */
    int id1 = 1, id2 = 2;
    coco_create(sched, sleep_coro, &id1, 0);
    coco_create(sched, sleep_coro, &id2, 0);

    /* Waiter ensures scheduler runs long enough */
    coco_create(sched, waiter_coro, NULL, 0);

    /* Run scheduler */
    coco_sched_run(sched);

    printf("\nTotal timer fires: %d (expected 3)\n", g_timer_count);
    printf("\n✅ Timer example completed\n");

    coco_sched_destroy(sched);
    return 0;
}
