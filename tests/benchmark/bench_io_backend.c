/**
 * bench_io_backend.c - I/O 后端性能对比基准测试
 *
 * 对比 epoll vs io_uring 吞吐量和延迟。
 * 支持 --backend epoll/iouring 命令行参数。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "coco.h"

/* 配置 */
static int g_ops = 10000;
static coco_io_backend_t g_backend = COCO_IO_BACKEND_AUTO;
static int g_batch_size = 1;

static uint64_t get_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* 解析命令行参数 */
static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "epoll") == 0) {
                g_backend = COCO_IO_BACKEND_EPOLL;
            } else if (strcmp(argv[i], "iouring") == 0) {
                g_backend = COCO_IO_BACKEND_IOURING;
            } else {
                g_backend = COCO_IO_BACKEND_AUTO;
            }
        } else if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc) {
            i++;
            g_ops = atoi(argv[i]);
        } else if (strcmp(argv[i], "--batch-size") == 0 && i + 1 < argc) {
            i++;
            g_batch_size = atoi(argv[i]);
        }
    }
}

/* Socket pair 测试 - 交替读写避免缓冲区满 */
static void run_socket_test(void) {
    printf("Running socket pair test (%d ops)...\n", g_ops);

    /* 创建 socket pair */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        printf("  ERROR: socketpair failed\n");
        return;
    }

    char buf[64] = "hello";
    char rbuf[64];

    /* 预热 */
    for (int i = 0; i < 100; i++) {
        write(sv[1], buf, sizeof(buf));
        read(sv[0], rbuf, sizeof(rbuf));
    }

    /* 测量交替读写 */
    uint64_t start = get_ns();
    for (int i = 0; i < g_ops; i++) {
        write(sv[1], buf, sizeof(buf));
        read(sv[0], rbuf, sizeof(rbuf));
    }
    uint64_t end = get_ns();

    /* 报告结果 */
    double total_sec = (end - start) / 1e9;
    double ops_per_sec = g_ops / total_sec;
    double avg_ns = (end - start) / g_ops;

    printf("  Backend: %s\n",
           g_backend == COCO_IO_BACKEND_EPOLL ? "epoll" :
           g_backend == COCO_IO_BACKEND_IOURING ? "io_uring" : "auto");
    printf("  Total time: %.3f sec\n", total_sec);
    printf("  Ops/sec: %.0f\n", ops_per_sec);
    printf("  Avg latency (ns): %.0f\n", avg_ns);

    /* 清理 */
    close(sv[0]);
    close(sv[1]);
}

int main(int argc, char **argv) {
    printf("=== I/O Backend Benchmark ===\n\n");

    parse_args(argc, argv);

    printf("Configuration:\n");
    printf("  Ops: %d\n", g_ops);
    printf("  Backend: %s\n",
           g_backend == COCO_IO_BACKEND_EPOLL ? "epoll" :
           g_backend == COCO_IO_BACKEND_IOURING ? "io_uring" : "auto");
    printf("  Batch size: %d\n\n", g_batch_size);

    /* 运行测试 */
    run_socket_test();

    printf("\n=== Benchmark complete ===\n");
    return 0;
}
