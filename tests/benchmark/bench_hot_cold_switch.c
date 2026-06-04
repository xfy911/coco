/**
 * bench_hot_cold_switch.c - hot stack vs cold backup/restore switch latency
 */

#include "coco.h"
#include <stdio.h>
#include <time.h>

#define N_CORO 8
#define N_YIELD 100000

static void yielder(void *arg) {
    (void)arg;
    for (int i = 0; i < N_YIELD; i++) {
        coco_yield();
    }
}

int main(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    coco_sched_t *sched = coco_sched_create();
    for (int i = 0; i < N_CORO; i++) {
        coco_create(sched, yielder, NULL, 0);
    }
    coco_sched_run(sched);
    coco_sched_destroy(sched);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    double total_yields = (double)N_CORO * N_YIELD;
    double ns_per_yield = (elapsed * 1e9) / total_yields;

    printf("bench_hot_cold_switch: %.3fs  %.1f ns/yield  %d coroutines\n",
           elapsed, ns_per_yield, N_CORO);
    printf("bench_hot_cold_switch: done\n");
    return 0;
}
