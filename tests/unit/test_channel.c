/**
 * test_channel.c - Channel 单元测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

static int recv_count = 0;
static int send_count = 0;

void sender_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    for (int i = 0; i < 5; i++) {
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

void test_channel_create(void) {
    printf("test_channel_create: ");

    /* 无缓冲 channel */
    coco_channel_t *ch1 = coco_channel_create(0);
    assert(ch1 != NULL);
    coco_channel_destroy(ch1);

    /* 有缓冲 channel */
    coco_channel_t *ch2 = coco_channel_create(10);
    assert(ch2 != NULL);
    coco_channel_destroy(ch2);

    printf("PASS\n");
}

void test_channel_send_recv(void) {
    printf("test_channel_send_recv: ");

    recv_count = 0;
    send_count = 0;

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(5);

    coco_create(sched, sender_coro, ch, 0);
    coco_create(sched, receiver_coro, ch, 0);

    coco_sched_run(sched);

    assert(send_count == 5);
    assert(recv_count == 5);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_channel_close(void) {
    printf("test_channel_close: ");

    coco_channel_t *ch = coco_channel_create(0);
    assert(ch != NULL);

    /* 关闭空 channel */
    coco_channel_close(ch);

    /* 向已关闭 channel 发送应失败 */
    int value = 42;
    int result = coco_channel_send(ch, &value);
    assert(result == COCO_ERROR_CHANNEL_CLOSED);

    coco_channel_destroy(ch);
    printf("PASS\n");
}

int main(void) {
    printf("=== Channel Tests ===\n");
    test_channel_create();
    test_channel_send_recv();
    test_channel_close();
    printf("All tests passed!\n");
    return 0;
}