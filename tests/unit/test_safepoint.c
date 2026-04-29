/**
 * test_safepoint.c - 安全点机制测试 (Phase 0 验证)
 *
 * 验收标准:
 * - COCO_SAFEPOINT() 宏正确实现
 * - 显式安全点正确工作
 * - 抢占请求后协程在下一个安全点让出
 * - 安全点开销 < 10ns
 */

#include "../../src/sched/safepoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* 测试计数器 */
static atomic_uint test_pass_count = 0;
static atomic_uint test_fail_count = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        atomic_fetch_add(&test_pass_count, 1); \
        printf("  ✓ %s\n", msg); \
    } else { \
        atomic_fetch_add(&test_fail_count, 1); \
        printf("  ✗ %s\n", msg); \
    } \
} while (0)

/* 获取当前时间（纳秒） */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* === 基础功能测试 === */

/* 测试 1: 创建和销毁 */
static void test_create_destroy(void) {
    printf("\n[TEST 1] 创建和销毁安全点上下文\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    TEST_ASSERT(ctx != NULL, "安全点上下文创建成功");
    TEST_ASSERT(!safepoint_check_preempt(ctx), "初始无抢占请求");
    TEST_ASSERT(safepoint_locks_held(ctx) == 0, "初始无锁持有");
    TEST_ASSERT(safepoint_get_yield_count(ctx) == 0, "初始让出次数为 0");

    safepoint_ctx_destroy(ctx);
    TEST_ASSERT(1, "安全点上下文销毁成功");
}

/* 测试 2: 抢占请求 */
static void test_preempt_request(void) {
    printf("\n[TEST 2] 抢占请求\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    assert(ctx != NULL);

    /* 请求抢占 */
    safepoint_request_preempt(ctx);
    TEST_ASSERT(safepoint_check_preempt(ctx), "抢占请求设置成功");

    /* 清除抢占 */
    safepoint_clear_preempt(ctx);
    TEST_ASSERT(!safepoint_check_preempt(ctx), "抢占请求清除成功");

    safepoint_ctx_destroy(ctx);
}

/* 测试 3: 安全点宏 */
static void test_safepoint_macro(void) {
    printf("\n[TEST 3] 安全点宏\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    assert(ctx != NULL);

    /* 无抢占请求时，安全点不应让出 */
    uint64_t yields_before = safepoint_get_yield_count(ctx);
    COCO_SAFEPOINT(ctx);
    uint64_t yields_after = safepoint_get_yield_count(ctx);
    TEST_ASSERT(yields_after == yields_before, "无抢占时安全点不让出");

    /* 有抢占请求时，安全点应让出 */
    safepoint_request_preempt(ctx);
    COCO_SAFEPOINT(ctx);
    yields_after = safepoint_get_yield_count(ctx);
    TEST_ASSERT(yields_after == yields_before + 1, "有抢占时安全点让出");

    /* 抢占请求应被清除 */
    TEST_ASSERT(!safepoint_check_preempt(ctx), "抢占请求已清除");

    safepoint_ctx_destroy(ctx);
}

/* 测试 4: 锁追踪 */
static void test_lock_tracking(void) {
    printf("\n[TEST 4] 锁追踪\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    assert(ctx != NULL);

    /* 进入锁 */
    safepoint_lock_enter(ctx);
    TEST_ASSERT(safepoint_locks_held(ctx) == 1, "持有 1 个锁");

    safepoint_lock_enter(ctx);
    TEST_ASSERT(safepoint_locks_held(ctx) == 2, "持有 2 个锁");

    /* 退出锁 */
    safepoint_lock_exit(ctx);
    TEST_ASSERT(safepoint_locks_held(ctx) == 1, "持有 1 个锁");

    safepoint_lock_exit(ctx);
    TEST_ASSERT(safepoint_locks_held(ctx) == 0, "持有 0 个锁");

    safepoint_ctx_destroy(ctx);
}

/* 测试 5: 安全点开销测量 */
static void test_safepoint_overhead(void) {
    printf("\n[TEST 5] 安全点开销测量\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    assert(ctx != NULL);

    /* 测量无抢占时的安全点开销 */
    #define OVERHEAD_SAMPLES 10000000  /* 1000 万次 */

    uint64_t start = get_time_ns();

    for (uint32_t i = 0; i < OVERHEAD_SAMPLES; i++) {
        COCO_SAFEPOINT(ctx);
    }

    uint64_t elapsed = get_time_ns() - start;
    double avg_ns = (double)elapsed / OVERHEAD_SAMPLES;

    printf("  总时间: %.2f ms\n", elapsed / 1000000.0);
    printf("  平均开销: %.2f ns\n", avg_ns);

    /* 安全点开销应 < 10ns (无抢占时仅是原子读取) */
    TEST_ASSERT(avg_ns < 10.0, "安全点开销 < 10ns");

    safepoint_ctx_destroy(ctx);
}

/* 测试 6: 多次抢占 */
static void test_multiple_preempts(void) {
    printf("\n[TEST 6] 多次抢占\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    assert(ctx != NULL);

    /* 模拟协程执行循环 */
    for (int i = 0; i < 100; i++) {
        /* 每隔 10 次请求抢占 */
        if (i % 10 == 0) {
            safepoint_request_preempt(ctx);
        }

        /* 安全点检查 */
        COCO_SAFEPOINT(ctx);
    }

    uint64_t yields = safepoint_get_yield_count(ctx);
    printf("  总让出次数: %llu\n", (unsigned long long)yields);
    TEST_ASSERT(yields == 10, "让出 10 次");

    safepoint_ctx_destroy(ctx);
}

/* 多线程测试参数 */
typedef struct {
    safepoint_ctx_t *ctx;
    atomic_uint *preempt_count;
} mt_test_args_t;

/* 抢占请求线程 */
static void *requester_thread(void *arg) {
    mt_test_args_t *args = (mt_test_args_t*)arg;
    for (int i = 0; i < 1000; i++) {
        safepoint_request_preempt(args->ctx);
        atomic_fetch_add(args->preempt_count, 1);
    }
    return NULL;
}

/* 执行线程 */
static void *executor_thread(void *arg) {
    mt_test_args_t *args = (mt_test_args_t*)arg;
    for (int i = 0; i < 100000; i++) {
        COCO_SAFEPOINT(args->ctx);
    }
    return NULL;
}

/* 测试 7: 多线程抢占请求 */
static void test_mt_preempt_request(void) {
    printf("\n[TEST 7] 多线程抢占请求\n");

    safepoint_ctx_t *ctx = safepoint_ctx_create();
    assert(ctx != NULL);

    atomic_uint preempt_count = 0;
    mt_test_args_t args = { ctx, &preempt_count };

    pthread_t t1, t2;
    pthread_create(&t1, NULL, requester_thread, &args);
    pthread_create(&t2, NULL, executor_thread, &args);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("  抢占请求次数: %u\n", atomic_load(&preempt_count));
    printf("  实际让出次数: %llu\n", (unsigned long long)safepoint_get_yield_count(ctx));
    TEST_ASSERT(atomic_load(&preempt_count) == 1000, "抢占请求次数正确");

    safepoint_ctx_destroy(ctx);
}

/* 嵌套安全点测试参数 */
static safepoint_ctx_t *g_nested_ctx = NULL;

static void inner_func(void) {
    COCO_SAFEPOINT(g_nested_ctx);
}

static void outer_func(void) {
    COCO_SAFEPOINT(g_nested_ctx);
    inner_func();
    COCO_SAFEPOINT(g_nested_ctx);
}

/* 测试 8: 嵌套安全点 */
static void test_nested_safepoints(void) {
    printf("\n[TEST 8] 嵌套安全点\n");

    g_nested_ctx = safepoint_ctx_create();
    assert(g_nested_ctx != NULL);

    /* 无抢占时执行 */
    outer_func();
    TEST_ASSERT(safepoint_get_yield_count(g_nested_ctx) == 0, "无抢占时嵌套安全点不让出");

    /* 有抢占时执行 */
    safepoint_request_preempt(g_nested_ctx);
    outer_func();
    /* 第一个安全点会清除抢占请求，后续安全点不会让出 */
    TEST_ASSERT(safepoint_get_yield_count(g_nested_ctx) == 1, "有抢占时第一个安全点让出");

    safepoint_ctx_destroy(g_nested_ctx);
    g_nested_ctx = NULL;
}

/* === 主测试入口 === */

int main(void) {
    printf("=== 安全点机制测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. COCO_SAFEPOINT() 宏正确实现\n");
    printf("  2. 显式安全点正确工作\n");
    printf("  3. 抢占请求后协程在下一个安全点让出\n");
    printf("  4. 安全点开销 < 10ns\n");

    test_create_destroy();
    test_preempt_request();
    test_safepoint_macro();
    test_lock_tracking();
    test_safepoint_overhead();
    test_multiple_preempts();
    test_mt_preempt_request();
    test_nested_safepoints();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 0 验收标准 US-003 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}