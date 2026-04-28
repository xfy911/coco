/**
 * bench_io.c - I/O 性能基准测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define OPS_COUNT 100000

static volatile int counter = 0;

void reader_coro(void *arg) {
    int fd = *(int*)arg;
    char buf[64];

    for (int i = 0; i < OPS_COUNT; i++) {
        int n = coco_read(fd, buf, sizeof(buf));
        if (n > 0) {
            counter++;
        }
    }
}

void writer_coro(void *arg) {
    int fd = *(int*)arg;
    const char *msg = "bench";

    for (int i = 0; i < OPS_COUNT; i++) {
        coco_write(fd, msg, strlen(msg));
    }
}

int main(void) {
    printf("=== I/O Benchmark ===\n");
    printf("Target: > 10000 req/s for echo server\n");
    printf("Iterations: %d\n", OPS_COUNT * 2);

    int pipefd[2];
    pipe(pipefd);

    coco_sched_t *sched = coco_sched_create();

    counter = 0;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    coco_create(sched, reader_coro, &pipefd[0], 0);
    coco_create(sched, writer_coro, &pipefd[1], 0);

    coco_sched_run(sched);

    clock_gettime(CLOCK_MONOTONIC, &end);

    close(pipefd[0]);
    close(pipefd[1]);
    coco_sched_destroy(sched);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    double ops_per_sec = counter / elapsed;

    printf("Results:\n");
    printf("  Total time: %.3f s\n", elapsed);
    printf("  Operations: %d\n", counter);
    printf("  Ops/sec: %.0f\n", ops_per_sec);

    if (ops_per_sec > 10000) {
        printf("✅ PASS: %.0f ops/s > 10000 target\n", ops_per_sec);
    } else {
        printf("⚠️  Below target: %.0f ops/s < 10000\n", ops_per_sec);
    }

    return 0;
}