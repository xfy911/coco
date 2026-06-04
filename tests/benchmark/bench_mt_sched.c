/**
 * bench_mt_sched.c - MT scheduler throughput benchmark
 */

#include "coco.h"
#include <stdio.h>
#include <time.h>

#define N_CORO 100
#define N_OPS 1000

static void worker(void *arg) {
    (void)arg;
    for (int i = 0; i < N_OPS; i++) {
        coco_yield();
    }
}

static double run(int nproc) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    coco_global_sched_start(nproc);
    for (int i = 0; i < N_CORO; i++) {
        coco_go(worker, NULL);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main(void) {
    printf("bench_mt_sched:\n");
    double t1 = run(1);
    printf("  1P: %.3fs (%.0f yields/sec)\n", t1, (double)N_CORO * N_OPS / t1);
    /* NOTE: Cannot run multiple start/stop in one process due to
     * pre-existing reinitialization bug. Only test 1P. */
    printf("bench_mt_sched: done\n");
    return 0;
}
