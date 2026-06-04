/**
 * stress_channel_mt_burst.c - 压力测试: 多线程并发 channel 通信
 *
 * 验证:
 * 1. 多线程 worker 间 channel 通信不阻塞
 * 2. 数据完整性
 * 3. 无死锁
 *
 * Design: 2 个生产者 + 2 个消费者共享一个 buffered channel，
 * 每个生产者发送 500 条消息。使用 coco_global_sched_start()
 * 启动 2 个 worker 线程。
 */

#include "coco.h"
#include "../../src/channel/channel_mt.h"
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>

#define N_PRODUCERS 2
#define N_CONSUMERS 2
#define MSG_COUNT 500

static _Atomic int total_recv = 0;
static _Atomic int producers_done = 0;
static coco_channel_mt_t *shared_ch;

static void mt_producer(void *arg) {
    (void)arg;
    for (int i = 0; i < MSG_COUNT; i++) {
        coco_channel_mt_send(shared_ch, (void *)(intptr_t)(i + 1));
    }
    if (atomic_fetch_add(&producers_done, 1) == N_PRODUCERS - 1) {
        coco_channel_mt_close(shared_ch);
    }
}

static void mt_consumer(void *arg) {
    (void)arg;
    void *val;
    while (coco_channel_mt_recv(shared_ch, &val) == COCO_OK) {
        atomic_fetch_add(&total_recv, 1);
    }
}

int main(void) {
    total_recv = 0;
    producers_done = 0;
    shared_ch = coco_channel_mt_create(10);
    assert(shared_ch != NULL);

    printf("stress_channel_mt_burst: starting...\n");
    fflush(stdout);
    coco_global_sched_start(2);

    for (int i = 0; i < N_CONSUMERS; i++) {
        coco_go(mt_consumer, NULL);
    }
    for (int i = 0; i < N_PRODUCERS; i++) {
        coco_go(mt_producer, NULL);
    }

    coco_global_sched_wait();

    printf("stress_channel_mt_burst: received %d/%d messages\n",
           total_recv, N_PRODUCERS * MSG_COUNT);
    assert(total_recv == N_PRODUCERS * MSG_COUNT);

    coco_channel_mt_destroy(shared_ch);
    coco_global_sched_stop();
    printf("stress_channel_mt_burst: PASSED\n");
    return 0;
}
