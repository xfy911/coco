/**
 * bench_mt_sched.c - MT scheduler throughput benchmark
 *
 * 测试场景:
 * 1. 纯 yield (调度开销)
 * 2. CPU 密集 + yield (真实并行)
 */

#include "coco.h"
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#define N_CORO 100
#define N_OPS 1000
#define CPU_WORK 10000

static void worker(void *arg) {
    (void)arg;
    for (int i = 0; i < N_OPS; i++) {
        coco_yield();
    }
}

static void cpu_worker(void *arg) {
    volatile double *sum = (volatile double *)arg;
    for (int i = 0; i < N_OPS; i++) {
        double s = 0;
        for (int j = 0; j < CPU_WORK; j++) {
            s += j * 0.001;
        }
        *sum += s;
        coco_yield();
    }
}

static double elapsed_ns(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

static double run_yield(int nproc) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    coco_global_sched_start(nproc);
    for (int i = 0; i < N_CORO; i++) {
        coco_go(worker, NULL);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_ns(&start, &end);
}

static double run_cpu(int nproc) {
    struct timespec start, end;
    double sums[N_CORO];
    for (int i = 0; i < N_CORO; i++) sums[i] = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    coco_global_sched_start(nproc);
    for (int i = 0; i < N_CORO; i++) {
        coco_go(cpu_worker, &sums[i]);
    }
    coco_global_sched_wait();
    coco_global_sched_stop();
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_ns(&start, &end);
}

int main(void) {
    printf("bench_mt_sched:\n\n");

    printf("--- Pure yield ---\n");
    double t1 = run_yield(1);
    printf("  1P: %.3fs (%.0f yields/sec)\n", t1, (double)N_CORO * N_OPS / t1);
    double t4 = run_yield(4);
    printf("  4P: %.3fs (%.0f yields/sec)\n", t4, (double)N_CORO * N_OPS / t4);
    printf("  4P speedup: %.2fx\n\n", t1 / t4);

    printf("--- CPU + yield ---\n");
    t1 = run_cpu(1);
    printf("  1P: %.3fs\n", t1);
    t4 = run_cpu(4);
    printf("  4P: %.3fs\n", t4);
    printf("  4P speedup: %.2fx\n\n", t1 / t4);

    printf("bench_mt_sched: done\n");
    return 0;
}
