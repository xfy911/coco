/**
 * bench_io.c - I/O 性能基准测试（直接 pipe I/O）
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#define OPS_COUNT 100000
#define BUF_SIZE 64

int main(void) {
    printf("=== I/O Benchmark ===\n");
    printf("Iterations: %d\n", OPS_COUNT);

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return 1;
    }

    char write_buf[BUF_SIZE];
    char read_buf[BUF_SIZE];
    memset(write_buf, 'x', BUF_SIZE);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < OPS_COUNT; i++) {
        if (write(pipefd[1], write_buf, BUF_SIZE) != BUF_SIZE) {
            perror("write");
            break;
        }
        if (read(pipefd[0], read_buf, BUF_SIZE) != BUF_SIZE) {
            perror("read");
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    close(pipefd[0]);
    close(pipefd[1]);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    double ops_per_sec = OPS_COUNT / elapsed;

    printf("Results:\n");
    printf("  Total time: %.3f s\n", elapsed);
    printf("  Ops/sec: %.0f\n", ops_per_sec);
    printf("  Latency: %.0f ns/op\n", elapsed * 1e9 / OPS_COUNT);

    return 0;
}
