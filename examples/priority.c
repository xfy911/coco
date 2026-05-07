/**
 * priority.c - Coroutine priority example
 *
 * Demonstrates multi-level priority scheduling.
 */

#include "coco.h"
#include <stdio.h>

static int execution_order[10];
static int order_index = 0;

void high_priority_coro(void *arg) {
    int id = *(int*)arg;
    printf("[HIGH] Coroutine %d running\n", id);
    execution_order[order_index++] = id;
    coco_yield();
    printf("[HIGH] Coroutine %d resumed\n", id);
    execution_order[order_index++] = id + 10;
}

void normal_priority_coro(void *arg) {
    int id = *(int*)arg;
    printf("[NORMAL] Coroutine %d running\n", id);
    execution_order[order_index++] = id;
    coco_yield();
    printf("[NORMAL] Coroutine %d resumed\n", id);
    execution_order[order_index++] = id + 10;
}

void low_priority_coro(void *arg) {
    int id = *(int*)arg;
    printf("[LOW] Coroutine %d running\n", id);
    execution_order[order_index++] = id;
    coco_yield();
    printf("[LOW] Coroutine %d resumed\n", id);
    execution_order[order_index++] = id + 10;
}

void idle_priority_coro(void *arg) {
    int id = *(int*)arg;
    printf("[IDLE] Coroutine %d running (only when no other work)\n", id);
    execution_order[order_index++] = id;
}

int main(void) {
    printf("=== Priority Scheduling Example ===\n\n");
    printf("Creating coroutines with different priorities...\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    int id1 = 1, id2 = 2, id3 = 3, id4 = 4, id5 = 5, id6 = 6;

    /* Create coroutines with different priorities */
    /* Note: Higher priority coroutines run first */

    coco_coro_t *c1 = coco_create(sched, low_priority_coro, &id1, 0);
    coco_set_priority(c1, COCO_PRIORITY_LOW);

    coco_coro_t *c2 = coco_create(sched, high_priority_coro, &id2, 0);
    coco_set_priority(c2, COCO_PRIORITY_HIGH);

    coco_coro_t *c3 = coco_create(sched, normal_priority_coro, &id3, 0);
    coco_set_priority(c3, COCO_PRIORITY_NORMAL);

    coco_coro_t *c4 = coco_create(sched, high_priority_coro, &id4, 0);
    coco_set_priority(c4, COCO_PRIORITY_HIGH);

    coco_coro_t *c5 = coco_create(sched, normal_priority_coro, &id5, 0);
    coco_set_priority(c5, COCO_PRIORITY_NORMAL);

    coco_coro_t *c6 = coco_create(sched, idle_priority_coro, &id6, 0);
    coco_set_priority(c6, COCO_PRIORITY_IDLE);

    printf("Priority levels: HIGH=0, NORMAL=1, LOW=2, IDLE=3\n");
    printf("Expected order: HIGH coroutines first, then NORMAL, then LOW, finally IDLE\n\n");

    coco_sched_run(sched);

    printf("\nExecution order: ");
    for (int i = 0; i < order_index; i++) {
        printf("%d ", execution_order[i]);
    }
    printf("\n\n");

    printf("Priority of c1 (LOW): %d\n", coco_get_priority(c1));
    printf("Priority of c2 (HIGH): %d\n", coco_get_priority(c2));
    printf("Priority of c3 (NORMAL): %d\n", coco_get_priority(c3));

    coco_sched_destroy(sched);

    printf("\n✅ Priority example completed\n");
    return 0;
}
