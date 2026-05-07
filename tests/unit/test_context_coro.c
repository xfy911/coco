/**
 * test_context_coro.c - Context 与协程集成测试 (Phase 2, US-010)
 *
 * 验收标准:
 * - 协程创建时关联 context
 * - 协程取消检查正确
 * - 100 层嵌套 context 取消传播正确
 * - 超时 context 与协程集成
 */

#include "../../src/core/context_coro.h"
#include "../../src/core/context_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>

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

/* === 测试 === */

/* 测试 1: 协程设置和获取 context */
static void test_coro_context(void) {
    printf("\n[TEST 1] 协程设置和获取 context\n");

    coco_coro_t coro = {0};
    coco_context_t *ctx = coco_context_create(NULL);

    int ret = coco_coro_set_context(&coro, ctx);
    TEST_ASSERT(ret == COCO_OK, "设置 context 成功");

    coco_context_t *got = coco_coro_get_context(&coro);
    TEST_ASSERT(got == ctx, "获取 context 正确");

    coco_coro_set_context(&coro, NULL);
    coco_context_unref(ctx);
}

/* 测试 2: 协程取消检查 */
static void test_coro_should_cancel(void) {
    printf("\n[TEST 2] 协程取消检查\n");

    coco_coro_t coro = {0};
    coco_context_t *ctx = coco_context_create(NULL);

    coco_coro_set_context(&coro, ctx);

    TEST_ASSERT(!coco_coro_should_cancel(&coro), "初始不应取消");

    coco_context_cancel(ctx);
    TEST_ASSERT(coco_coro_should_cancel(&coro), "context 取消后应取消");

    coco_coro_set_context(&coro, NULL);
    coco_context_unref(ctx);
}

/* 测试 3: 协程自身取消标志 */
static void test_coro_cancel_flag(void) {
    printf("\n[TEST 3] 协程自身取消标志\n");

    coco_coro_t coro = {0};
    coro.cancelled = 0;

    TEST_ASSERT(!coco_coro_should_cancel(&coro), "初始不应取消");

    coro.cancelled = 1;
    TEST_ASSERT(coco_coro_should_cancel(&coro), "设置标志后应取消");
}

/* 测试 4: 深层嵌套取消传播 */
static void test_deep_cancel_propagation(void) {
    printf("\n[TEST 4] 深层嵌套取消传播 (100 层)\n");

    #define DEPTH 100
    coco_context_t **ctxs = malloc(DEPTH * sizeof(coco_context_t*));
    coco_coro_t *coros = malloc(DEPTH * sizeof(coco_coro_t));

    memset(coros, 0, DEPTH * sizeof(coco_coro_t));

    ctxs[0] = coco_context_create(NULL);
    coco_coro_set_context(&coros[0], ctxs[0]);

    for (int i = 1; i < DEPTH; i++) {
        coco_context_opts_t opts = { .parent = ctxs[i-1] };
        ctxs[i] = coco_context_create(&opts);
        coco_coro_set_context(&coros[i], ctxs[i]);
    }

    TEST_ASSERT(!coco_coro_should_cancel(&coros[DEPTH-1]), "最深层初始不应取消");

    /* 取消根 context */
    coco_context_cancel(ctxs[0]);

    /* 检查所有层级都已取消 */
    int all_cancelled = 1;
    for (int i = 0; i < DEPTH; i++) {
        if (!coco_coro_should_cancel(&coros[i])) {
            all_cancelled = 0;
            break;
        }
    }
    TEST_ASSERT(all_cancelled, "所有层级都已取消");

    /* 释放 */
    for (int i = DEPTH - 1; i >= 0; i--) {
        coco_coro_set_context(&coros[i], NULL);
        coco_context_unref(ctxs[i]);
    }

    free(ctxs);
    free(coros);
    #undef DEPTH
}

/* 测试 5: 超时 context 与协程 */
static void test_timeout_with_coro(void) {
    printf("\n[TEST 5] 超时 context 与协程\n");

    coco_context_t *ctx = coco_context_with_timeout(NULL, 50);
    coco_coro_t coro = {0};
    coco_coro_set_context(&coro, ctx);

    TEST_ASSERT(!coco_coro_should_cancel(&coro), "初始不应取消");

    /* 等待超时 */
    usleep(100000);  /* 100ms */

    TEST_ASSERT(coco_coro_should_cancel(&coro), "超时后应取消");

    coco_coro_set_context(&coro, NULL);
    coco_context_unref(ctx);
}

/* 测试 6: context 引用计数与协程 */
static void test_refcount_with_coro(void) {
    printf("\n[TEST 6] context 引用计数与协程\n");

    coco_context_t *ctx = coco_context_create(NULL);
    TEST_ASSERT(atomic_load(&ctx->refcount) == 1, "初始引用计数为 1");

    coco_coro_t coro = {0};
    coco_coro_set_context(&coro, ctx);
    TEST_ASSERT(atomic_load(&ctx->refcount) == 2, "设置后引用计数为 2");

    coco_coro_set_context(&coro, NULL);
    TEST_ASSERT(atomic_load(&ctx->refcount) == 1, "清除后引用计数为 1");

    coco_context_unref(ctx);
}

/* === 主测试入口 === */

int main(void) {
    printf("=== Context 与协程集成测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. 协程创建时关联 context\n");
    printf("  2. 协程取消检查正确\n");
    printf("  3. 100 层嵌套 context 取消传播正确\n");
    printf("  4. 超时 context 与协程集成\n");

    test_coro_context();
    test_coro_should_cancel();
    test_coro_cancel_flag();
    test_deep_cancel_propagation();
    test_timeout_with_coro();
    test_refcount_with_coro();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 2 验收标准 US-010 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}
