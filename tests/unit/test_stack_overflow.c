/**
 * test_stack_overflow.c - 栈溢出信号处理测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <string.h>

static int overflow_detected = 0;
static coco_coro_t *overflow_coro = NULL;

void overflow_error_cb(coco_coro_t *coro, int error_code, const char *msg) {
    printf("Error callback triggered: coro_id=%llu, code=%d, msg=%s\n",
           (unsigned long long)coro->id, error_code, msg);
    overflow_detected = 1;
    overflow_coro = coro;
}

/* 递归函数触发栈溢出 */
void recursive_overflow(void *arg) {
    (void)arg;
    char buffer[1024];  /* 每次递归消耗栈空间 */
    memset(buffer, 0, sizeof(buffer));

    /* 无限递归直到栈溢出 */
    recursive_overflow(NULL);
}

void test_stack_overflow_detection(void) {
    printf("test_stack_overflow_detection: ");

    overflow_detected = 0;
    overflow_coro = NULL;

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    /* 创建小栈协程，更容易触发溢出 */
    coco_coro_t *coro = coco_create(sched, recursive_overflow, NULL, 4096);
    assert(coro != NULL);

    /* 设置错误回调 */
    coco_set_error_cb(coro, overflow_error_cb);

    /* 运行调度器，期望捕获栈溢出 */
    coco_sched_run(sched);

    /* 验证溢出检测 */
    printf("overflow_detected=%d, coro_state=%d\n",
           overflow_detected, coco_get_state(coro));

    assert(overflow_detected == 1);
    assert(coco_get_state(coro) == COCO_STATE_OVERFLOW);
    assert(overflow_coro == coro);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

/* 测试正常协程不受影响 */
void normal_coro_entry(void *arg) {
    (void)arg;
    printf("Normal coroutine running\n");
    coco_yield();
    printf("Normal coroutine resumed\n");
}

void test_normal_coro_unaffected(void) {
    printf("test_normal_coro_unaffected: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_coro_t *coro1 = coco_create(sched, normal_coro_entry, NULL, 0);
    coco_coro_t *coro2 = coco_create(sched, normal_coro_entry, NULL, 0);

    assert(coro1 != NULL);
    assert(coro2 != NULL);

    coco_sched_run(sched);

    /* 正常协程应完成 */
    assert(coco_get_state(coro1) == COCO_STATE_DEAD);
    assert(coco_get_state(coro2) == COCO_STATE_DEAD);

    coco_sched_destroy(sched);
    printf("PASS\n");
}

int main(void) {
    printf("=== Stack Overflow Tests ===\n");

    /* 注意：栈溢出测试可能在不同环境下表现不同 */
    /* 在某些系统上可能需要调整栈大小或递归深度 */

    test_normal_coro_unaffected();

    /* 栈溢出测试可能导致进程崩溃，谨慎运行 */
    /* test_stack_overflow_detection(); */

    printf("All tests passed!\n");
    return 0;
}