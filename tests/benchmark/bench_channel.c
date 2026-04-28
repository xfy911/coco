/**
 * bench_channel.c - Channel 性能基准测试（简化版）
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define OPS_COUNT 1000

static volatile int send_count = 0;
static volatile int recv_count = 0;

void sender_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;

    for (int i = 0; i < OPS_COUNT; i++) {
        int *value = malloc(sizeof(int));
        *value = i;
        coco_channel_send(ch, value);
        send_count++;
    }
    coco_channel_close(ch);
}

void receiver_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    void *value;

    while (coco_channel_recv(ch, &value) == COCO_OK) {
        recv_count++;
        free(value);
    }
}

int main(void) {
    printf("=== Channel Benchmark ===\n");
    printf("Iterations: %d\n", OPS_COUNT);

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(100);

    send_count = 0;
    recv_count = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    coco_create(sched, sender_coro, ch, 0);
    coco_create(sched, receiver_coro, ch, 0);

    coco_sched_run(sched);

    clock_gettime(CLOCK_MONOTONIC, &end);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);

    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    int total_ops = send_count + recv_count;
    double ops_per_sec = total_ops / elapsed;

    printf("Results:\n");
    printf("  Total time: %.3f s\n", elapsed);
    printf("  Sends: %d\n", send_count);
    printf("  Receives: %d\n", recv_count);
    printf("  Ops/sec: %.0f\n", ops_per_sec);

    return 0;
}