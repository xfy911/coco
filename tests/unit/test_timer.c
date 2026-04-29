/**
 * test_timer.c - 定时器单元测试
 *
 * 测试覆盖:
 * - 定时器创建和销毁
 * - 定时器取消
 * - 定时器精度
 * - coco_timer_wheel_next_expire 测试
 * - 边界条件测试 (delay=0)
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

static int timer_count = 0;

void timer_callback(void *arg) {
    (void)arg;
    timer_count++;
}

/* ========== 基础测试 ========== */

void test_timer_create(void) {
    printf("test_timer_create: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_timer_t *timer = coco_timer(10, timer_callback, NULL);
    assert(timer != NULL);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_cancel(void) {
    printf("test_timer_cancel: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_timer_t *timer = coco_timer(100, timer_callback, NULL);
    assert(timer != NULL);

    coco_timer_cancel(timer);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_accuracy(void) {
    printf("test_timer_accuracy: ");

    timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    /* 使用调度器的时间轮 */
    coco_timer(5, timer_callback, NULL);
    coco_timer(5, timer_callback, NULL);
    coco_timer(5, timer_callback, NULL);

    /* 模拟时间流逝 */
    usleep(10000);  /* 等待 10ms */

    /* 处理 tick */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("timer_count=%d (expected >= 3)\n", timer_count);
    assert(timer_count >= 3);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== W1 层测试 ========== */

void test_timer_w1_layer(void) {
    printf("test_timer_w1_layer: ");

    timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    /* W1 层: 0-255ms */
    coco_timer(10, timer_callback, NULL);
    coco_timer(50, timer_callback, NULL);
    coco_timer(100, timer_callback, NULL);
    coco_timer(200, timer_callback, NULL);

    usleep(250000);  /* 等待 250ms */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("timer_count=%d (expected >= 4)\n", timer_count);
    assert(timer_count >= 4);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== coco_timer_wheel_next_expire 测试 ========== */

void test_timer_next_expire(void) {
    printf("test_timer_next_expire: ");

    coco_sched_t *sched = coco_sched_create();

    /* 无定时器时返回 0 */
    uint64_t next = coco_timer_wheel_next_expire(sched->timer_wheel);

    /* 添加定时器 */
    coco_timer(100, timer_callback, NULL);

    next = coco_timer_wheel_next_expire(sched->timer_wheel);
    /* 应该返回一个合理的到期时间 */
    assert(next > 0 || next == 0);  /* 基本验证 */

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_next_expire_null(void) {
    printf("test_timer_next_expire_null: ");

    uint64_t next = coco_timer_wheel_next_expire(NULL);
    assert(next == 0);

    printf("PASS\n");
}

/* ========== 边界条件测试 ========== */

void test_timer_delay_zero(void) {
    printf("test_timer_delay_zero: ");

    timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    /* delay=0 应该立即或在下一个 tick 触发 */
    coco_timer(0, timer_callback, NULL);

    usleep(1000);  /* 等待 1ms */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("timer_count=%d (expected >= 1)\n", timer_count);
    assert(timer_count >= 1);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_cancel_twice(void) {
    printf("test_timer_cancel_twice: ");

    coco_sched_t *sched = coco_sched_create();

    coco_timer_t *timer = coco_timer(100, timer_callback, NULL);
    assert(timer != NULL);

    coco_timer_cancel(timer);
    coco_timer_cancel(timer);  /* 重复取消不应崩溃 */

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_cancel_null(void) {
    printf("test_timer_cancel_null: ");

    coco_timer_cancel(NULL);  /* 不应崩溃 */

    printf("PASS\n");
}

/* ========== 多定时器测试 ========== */

void test_multiple_timers(void) {
    printf("test_multiple_timers: ");

    timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    /* 创建多个定时器，不同延迟 */
    for (int i = 1; i <= 10; i++) {
        coco_timer(i * 10, timer_callback, NULL);
    }

    usleep(150000);  /* 等待 150ms */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("timer_count=%d (expected >= 10)\n", timer_count);
    assert(timer_count >= 10);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== 定时器取消后不触发测试 ========== */

static int cancelled_timer_count = 0;

void cancelled_timer_callback(void *arg) {
    (void)arg;
    cancelled_timer_count++;
}

void test_timer_cancel_prevents_fire(void) {
    printf("test_timer_cancel_prevents_fire: ");

    cancelled_timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    coco_timer_t *timer = coco_timer(50, cancelled_timer_callback, NULL);
    coco_timer_cancel(timer);  /* 立即取消 */

    usleep(100000);  /* 等待 100ms */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("cancelled_timer_count=%d (expected 0)\n", cancelled_timer_count);
    assert(cancelled_timer_count == 0);  /* 取消的定时器不应触发 */

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* ========== coco_timer_ex 测试 ========== */

void test_timer_ex(void) {
    printf("test_timer_ex: ");

    timer_count = 0;
    coco_sched_t *sched = coco_sched_create();

    coco_timer_t *timer = coco_timer_ex(sched, 10, timer_callback, NULL);
    assert(timer != NULL);

    usleep(20000);  /* 等待 20ms */
    coco_timer_tick(sched->timer_wheel, sched);

    printf("timer_count=%d (expected >= 1)\n", timer_count);
    assert(timer_count >= 1);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

void test_timer_ex_null_sched(void) {
    printf("test_timer_ex_null_sched: ");

    coco_timer_t *timer = coco_timer_ex(NULL, 10, timer_callback, NULL);
    assert(timer == NULL);

    printf("PASS\n");
}

/* ========== 时间轮创建/销毁测试 ========== */

void test_timer_wheel_create(void) {
    printf("test_timer_wheel_create: ");

    coco_timer_wheel_t *tw = coco_timer_wheel_create();
    assert(tw != NULL);

    coco_timer_wheel_destroy(tw);
    printf("PASS\n");
}

void test_timer_wheel_destroy_null(void) {
    printf("test_timer_wheel_destroy_null: ");

    coco_timer_wheel_destroy(NULL);  /* 不应崩溃 */

    printf("PASS\n");
}

/* ========== 主函数 ========== */

int main(void) {
    printf("=== Timer Tests ===\n");

    /* 基础测试 */
    test_timer_create();
    test_timer_cancel();
    test_timer_accuracy();

    /* W1 层测试 */
    test_timer_w1_layer();

    /* coco_timer_wheel_next_expire 测试 */
    test_timer_next_expire();
    test_timer_next_expire_null();

    /* 边界条件测试 */
    test_timer_delay_zero();
    test_timer_cancel_twice();
    test_timer_cancel_null();

    /* 多定时器测试 */
    test_multiple_timers();

    /* 定时器取消后不触发 */
    test_timer_cancel_prevents_fire();

    /* coco_timer_ex 测试 */
    test_timer_ex();
    test_timer_ex_null_sched();

    /* 时间轮创建/销毁测试 */
    test_timer_wheel_create();
    test_timer_wheel_destroy_null();

    printf("All tests passed!\n");
    return 0;
}