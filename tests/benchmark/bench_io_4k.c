/**
 * bench_io.c - 协程 I/O 性能基准测试
 *
 * 使用 socketpair + coco_read/coco_write 测量实际协程 I/O 吞吐量。
 */

#include "coco.h"
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

#define N_CORO 4
#define N_MSG 10000
#define MSG_SIZE 4096

static int sv[2]; /* socketpair */

static void writer(void *arg) {
    (void)arg;
    char buf[MSG_SIZE] = "hello";
    for (int i = 0; i < N_MSG; i++) {
        int n = coco_write(sv[0], buf, MSG_SIZE);
        assert(n == MSG_SIZE);
    }
}

static void reader(void *arg) {
    (void)arg;
    char buf[MSG_SIZE];
    for (int i = 0; i < N_MSG; i++) {
        int n = coco_read(sv[1], buf, MSG_SIZE);
        assert(n == MSG_SIZE);
    }
}

int main(void) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        perror("socketpair");
        return 1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    coco_sched_t *sched = coco_sched_create();
    for (int i = 0; i < N_CORO; i++) {
        coco_create(sched, writer, NULL, 0);
        coco_create(sched, reader, NULL, 0);
    }
    coco_sched_run(sched);
    coco_sched_destroy(sched);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    double total_bytes = 2.0 * N_CORO * N_MSG * MSG_SIZE;
    double throughput_mbps = (total_bytes / elapsed) / (1024 * 1024);

    printf("bench_io: %.3fs  %.1f MB/s  %d coroutine I/O pairs\n",
           elapsed, throughput_mbps, N_CORO);

    close(sv[0]);
    close(sv[1]);
    return 0;
}
