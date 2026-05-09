/**
 * fan_out.c - Fan-out/Fan-in 模式示例
 *
 * 展示使用 Channel 实现 fan-out/fan-in 并行模式：
 * 一个生产者分发任务到多个 worker，再由聚合器收集结果。
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define NUM_WORKERS 4
#define NUM_TASKS   12

typedef struct {
    int task_id;
    int input;
} task_t;

typedef struct {
    int task_id;
    int result;
} result_t;

typedef struct {
    coco_channel_t *task_ch;
    coco_channel_t *result_ch;
    coco_channel_t *done_ch;
    int worker_id;
} worker_ctx_t;

/* 生产者：向任务 channel 发送任务 */
void producer(void *arg) {
    coco_channel_t *task_ch = (coco_channel_t *)arg;

    for (int i = 0; i < NUM_TASKS; i++) {
        task_t *t = malloc(sizeof(task_t));
        t->task_id = i;
        t->input = (i + 1) * (i + 1);

        printf("Producer: sending task %d (input=%d)\n", t->task_id, t->input);
        coco_channel_send(task_ch, t);
    }

    coco_channel_close(task_ch);
    printf("Producer: all tasks sent, channel closed\n");
}

/* Worker：从任务 channel 读取，处理后发送结果 */
void worker(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;

    void *val = NULL;
    while (coco_channel_recv(ctx->task_ch, &val) == COCO_OK) {
        task_t *t = (task_t *)val;

        result_t *r = malloc(sizeof(result_t));
        r->task_id = t->task_id;
        r->result = t->input * 2;

        printf("  Worker %d: task %d -> result %d\n", ctx->worker_id, t->task_id, r->result);
        coco_channel_send(ctx->result_ch, r);

        free(t);
    }

    printf("  Worker %d: exiting (channel closed)\n", ctx->worker_id);
    /* 通知 closer 当前 worker 已完成 */
    coco_channel_send(ctx->done_ch, (void *)(intptr_t)ctx->worker_id);
}

/* Closer：等待所有 worker 完成后关闭 result channel */
void closer(void *arg) {
    worker_ctx_t *ctxs = (worker_ctx_t *)arg;
    coco_channel_t *done_ch = ctxs[0].done_ch;

    void *val = NULL;
    for (int i = 0; i < NUM_WORKERS; i++) {
        coco_channel_recv(done_ch, &val);
    }

    printf("Closer: all workers done, closing result channel\n");
    coco_channel_close(ctxs[0].result_ch);
}

/* 聚合器：收集所有 worker 的结果 */
void collector(void *arg) {
    coco_channel_t *result_ch = (coco_channel_t *)arg;
    int total = 0;
    int count = 0;
    void *val = NULL;

    while (coco_channel_recv(result_ch, &val) == COCO_OK) {
        result_t *r = (result_t *)val;
        printf("Collector: task %d result = %d\n", r->task_id, r->result);
        total += r->result;
        count++;
        free(r);
    }

    printf("\nCollector: %d results collected, total = %d\n", count, total);
}

int main(void) {
    printf("=== Fan-out/Fan-in Example ===\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    coco_channel_t *task_ch = coco_channel_create(8);
    coco_channel_t *result_ch = coco_channel_create(8);
    coco_channel_t *done_ch = coco_channel_create(NUM_WORKERS);

    /* 生产者 */
    coco_create(sched, producer, task_ch, 0);

    /* 多个 worker（fan-out） */
    static worker_ctx_t worker_ctxs[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        worker_ctxs[i].task_ch = task_ch;
        worker_ctxs[i].result_ch = result_ch;
        worker_ctxs[i].done_ch = done_ch;
        worker_ctxs[i].worker_id = i + 1;
        coco_create(sched, worker, &worker_ctxs[i], 0);
    }

    /* Closer：等待所有 worker 退出后关闭 result_ch */
    coco_create(sched, closer, worker_ctxs, 0);

    /* 聚合器（fan-in） */
    coco_create(sched, collector, result_ch, 0);

    coco_sched_run(sched);

    coco_channel_destroy(task_ch);
    coco_channel_destroy(result_ch);
    coco_channel_destroy(done_ch);
    coco_sched_destroy(sched);

    printf("\n✅ Fan-out/Fan-in example completed\n");
    return 0;
}
