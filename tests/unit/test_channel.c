/**
 * test_channel.c - Channel 单元测试
 *
 * 测试覆盖:
 * - 有缓冲/无缓冲 channel 创建
 * - send/recv 基本操作
 * - channel 关闭行为
 * - NULL 参数错误处理
 * - 已关闭 channel 的 recv 行为
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int recv_count = 0;
static int send_count = 0;

/* ========== 协程函数定义 ========== */

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

/* ========== 基础测试 ========== */

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

/* ========== 无缓冲 channel 测试 ========== */

static volatile int unbuffered_recv_done = 0;
static volatile int unbuffered_send_done = 0;

void unbuffered_sender(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    int value = 123;
    int ret = coco_channel_send(ch, &value);
    if (ret == COCO_OK) {
        unbuffered_send_done = 1;
    }
}

void unbuffered_receiver(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    void *value = NULL;
    int ret = coco_channel_recv(ch, &value);
    if (ret == COCO_OK && value != NULL && *(int*)value == 123) {
        unbuffered_recv_done = 1;
    }
}

void test_unbuffered_channel(void) {
    printf("test_unbuffered_channel: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(0);  /* 无缓冲 */
    unbuffered_recv_done = 0;
    unbuffered_send_done = 0;

    /* 先创建接收者，再创建发送者 */
    coco_create(sched, unbuffered_receiver, ch, 0);
    coco_create(sched, unbuffered_sender, ch, 0);

    coco_sched_run(sched);

    assert(unbuffered_send_done == 1);
    assert(unbuffered_recv_done == 1);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== NULL 参数错误处理测试 ========== */

void test_channel_send_null(void) {
    printf("test_channel_send_null: ");

    int value = 42;
    int ret = coco_channel_send(NULL, &value);
    assert(ret == COCO_ERROR_CHANNEL_CLOSED);

    printf("PASS\n");
}

void test_channel_recv_null(void) {
    printf("test_channel_recv_null: ");

    void *value = NULL;
    int ret = coco_channel_recv(NULL, &value);
    assert(ret == COCO_ERROR);

    coco_channel_t *ch = coco_channel_create(0);
    ret = coco_channel_recv(ch, NULL);
    assert(ret == COCO_ERROR);
    coco_channel_destroy(ch);

    printf("PASS\n");
}

void test_channel_close_null(void) {
    printf("test_channel_close_null: ");

    coco_channel_close(NULL);  /* 不应崩溃 */
    coco_channel_close(NULL);  /* 重复调用也不崩溃 */

    printf("PASS\n");
}

void test_channel_destroy_null(void) {
    printf("test_channel_destroy_null: ");

    coco_channel_destroy(NULL);  /* 不应崩溃 */

    printf("PASS\n");
}

/* ========== 已关闭 channel 的 recv 行为测试 ========== */

static volatile int closed_recv_count = 0;

void closed_channel_receiver(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    void *value = NULL;
    int ret = coco_channel_recv(ch, &value);
    if (ret == COCO_ERROR_CHANNEL_CLOSED) {
        closed_recv_count = 1;
    }
}

void test_recv_closed_empty_channel(void) {
    printf("test_recv_closed_empty_channel: ");

    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(0);
    coco_channel_close(ch);

    closed_recv_count = 0;
    coco_create(sched, closed_channel_receiver, ch, 0);
    coco_sched_run(sched);

    assert(closed_recv_count == 1);

    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== 双重关闭测试 ========== */

void test_channel_double_close(void) {
    printf("test_channel_double_close: ");

    coco_channel_t *ch = coco_channel_create(5);
    assert(ch != NULL);

    coco_channel_close(ch);

    /* 再次关闭不应崩溃 */
    coco_channel_close(ch);

    coco_channel_destroy(ch);
    printf("PASS\n");
}

/* ========== 主函数 ========== */

int main(void) {
    printf("=== Channel Tests ===\n");

    /* 基础测试 */
    test_channel_create();
    test_channel_send_recv();
    test_channel_close();

    /* 无缓冲 channel 测试 */
    test_unbuffered_channel();

    /* NULL 参数错误处理 */
    test_channel_send_null();
    test_channel_recv_null();
    test_channel_close_null();
    test_channel_destroy_null();

    /* 已关闭 channel 的 recv 行为 */
    test_recv_closed_empty_channel();

    /* 双重关闭 */
    test_channel_double_close();

    printf("All tests passed!\n");
    return 0;
}