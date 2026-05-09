/**
 * stress_channel_burst.c - 压力测试: 多协程并发 channel 通信
 *
 * 验证:
 * 1. 多协程并发 channel 通信不阻塞
 * 2. 数据完整性
 * 3. 无死锁
 *
 * Design: 每个发送-接收配对通过独立 channel 通信。
 * 发送完成后关闭 channel，接收者检测关闭后退出。
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdatomic.h>

static atomic_int total_sent = 0;
static atomic_int total_received = 0;

typedef struct {
    coco_channel_t *ch;
    int items;
    int start_id;
} sender_ctx_t;

void sender(void *arg) {
    sender_ctx_t *ctx = (sender_ctx_t *)arg;
    for (int j = 0; j < ctx->items; j++) {
        int *val = malloc(sizeof(int));
        *val = ctx->start_id * ctx->items + j;
        int ret = coco_channel_send(ctx->ch, val);
        if (ret != COCO_OK) {
            free(val);
            break;
        }
        atomic_fetch_add(&total_sent, 1);
    }
    coco_channel_close(ctx->ch);
    free(ctx);
}

void receiver(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    int *val;
    while (coco_channel_recv(ch, (void**)&val) == COCO_OK) {
        assert(*val >= 0);
        free(val);
        atomic_fetch_add(&total_received, 1);
    }
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    const int CHANNELS = 100;
    const int ITEMS_PER_CH = 50;

    printf("Stress test: %d channels x %d items = %d messages\n",
           CHANNELS, ITEMS_PER_CH, CHANNELS * ITEMS_PER_CH);

    for (int i = 0; i < CHANNELS; i++) {
        coco_channel_t *ch = coco_channel_create(ITEMS_PER_CH);
        assert(ch != NULL);
        coco_create(sched, receiver, ch, 0);

        sender_ctx_t *ctx = malloc(sizeof(sender_ctx_t));
        ctx->ch = ch;
        ctx->items = ITEMS_PER_CH;
        ctx->start_id = i;

        coco_create(sched, sender, ctx, 0);
    }

    /* 运行 */
    coco_sched_run(sched);

    int sent = atomic_load(&total_sent);
    int received = atomic_load(&total_received);

    printf("  Sent: %d, Received: %d\n", sent, received);
    assert(received == CHANNELS * ITEMS_PER_CH);

    coco_sched_destroy(sched);

    printf("[PASS] Channel burst: %d messages completed\n", received);
    return 0;
}
