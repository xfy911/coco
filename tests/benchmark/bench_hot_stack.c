/**
 * bench_hot_stack.c - Hot stack yield benchmark
 */

#include "../src/coco_internal.h"
#include "../src/core/hot_stack.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_COROS  8
#define YIELDS_PER 100000
#define STACK_SIZE 0

static volatile int yields_done = 0;

static void yielder(void *arg) {
    int count = *(int *)arg;
    for (int i = 0; i < count; i++) {
        __sync_fetch_and_add((int *)&yields_done, 1);
        coco_yield();
    }
}

int main(void) {
    printf("=== Hot Stack Benchmark ===\n");
    printf("Coroutines: %d, Yields/coro: %d, Hot slots: %d\n\n",
           NUM_COROS, YIELDS_PER, COCO_HOT_SLOTS_DEFAULT);
    fflush(stdout);

    int yields = YIELDS_PER;
    coco_sched_t *sched = coco_sched_create();

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < NUM_COROS; i++) {
        coco_create(sched, yielder, &yields, STACK_SIZE);
    }

    coco_sched_run(sched);

    clock_gettime(CLOCK_MONOTONIC, &end);

    int total_yields = NUM_COROS * YIELDS_PER;
    double elapsed_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    double ns_per_yield = elapsed_ns / total_yields;

    printf("Total time:    %.3f ms\n", elapsed_ns / 1e6);
    printf("Total yields:  %d (actual: %d)\n", total_yields, yields_done);
    printf("Per yield:     %.2f ns\n", ns_per_yield);
    printf("Hot slots:     %d\n", sched->hot_slot_count);
    fflush(stdout);

    coco_sched_destroy(sched);

    return 0;
}
