/**
 * pipeline.c - Channel 管道示例
 *
 * 展示使用 Channel 实现协程间通信的管道模式。
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>

/* 生产者协程 */
void producer(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;

    for (int i = 1; i <= 10; i++) {
        int *value = malloc(sizeof(int));
        *value = i;

        printf("Producer: sending %d\n", i);
        coco_channel_send(ch, value);
    }

    printf("Producer: closing channel\n");
    coco_channel_close(ch);
}

/* 处理器协程（转换数据） */
void processor(void *arg) {
    coco_channel_t **channels = (coco_channel_t**)arg;
    coco_channel_t *in_ch = channels[0];
    coco_channel_t *out_ch = channels[1];

    void *value;
    while (coco_channel_recv(in_ch, &value) == COCO_OK) {
        int *data = (int*)value;
        *data *= 2;  /* 转换：乘以 2 */

        printf("Processor: transformed %d -> %d\n", *data / 2, *data);
        coco_channel_send(out_ch, data);
    }

    printf("Processor: input closed, closing output\n");
    coco_channel_close(out_ch);
}

/* 消费者协程 */
void consumer(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;

    void *value;
    int sum = 0;
    while (coco_channel_recv(ch, &value) == COCO_OK) {
        int *data = (int*)value;
        printf("Consumer: received %d\n", *data);
        sum += *data;
        free(data);
    }

    printf("Consumer: channel closed, total sum = %d\n", sum);
}

int main(void) {
    printf("=== Pipeline Example ===\n\n");
    printf("Producer -> Processor -> Consumer\n\n");

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    /* 创建两个 channel 连接三个阶段 */
    coco_channel_t *ch1 = coco_channel_create(5);  /* 生产者 -> 处理器 */
    coco_channel_t *ch2 = coco_channel_create(5);  /* 处理器 -> 消费者 */

    /* 创建协程 */
    coco_channel_t *channels[2] = {ch1, ch2};
    coco_create(sched, producer, ch1, 0);
    coco_create(sched, processor, channels, 0);
    coco_create(sched, consumer, ch2, 0);

    /* 运行管道 */
    coco_sched_run(sched);

    /* 清理 */
    coco_channel_destroy(ch1);
    coco_channel_destroy(ch2);
    coco_sched_destroy(sched);

    return 0;
}