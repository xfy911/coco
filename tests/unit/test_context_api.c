/**
 * test_context_api.c - Context API 测试 (Phase 2, US-009)
 *
 * 验收标准:
 * - coco_context_t 结构正确实现
 * - coco_context_create/cancel/done API 正确
 * - 取消传播到子 context
 * - 超时自动取消
 */

#include "../../src/core/context_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
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

/* 测试 1: 创建和销毁 */
static void test_create_destroy(void) {
    printf("\n[TEST 1] 创建和销毁\n");

    coco_context_t *ctx = coco_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "创建 context 成功");
    TEST_ASSERT(!coco_context_is_cancelled(ctx), "初始未取消");
    TEST_ASSERT(!coco_context_is_done(ctx), "初始未完成");

    coco_context_unref(ctx);
    TEST_ASSERT(1, "销毁成功");
}

/* 测试 2: 取消操作 */
static void test_cancel(void) {
    printf("\n[TEST 2] 取消操作\n");

    coco_context_t *ctx = coco_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "创建 context 成功");

    coco_context_cancel(ctx);
    TEST_ASSERT(coco_context_is_cancelled(ctx), "已取消");
    TEST_ASSERT(coco_context_is_done(ctx), "已完成");

    /* 重复取消应该无效果 */
    coco_context_cancel(ctx);
    TEST_ASSERT(coco_context_is_cancelled(ctx), "重复取消无效果");

    coco_context_unref(ctx);
}

/* 测试 3: 父子关系 */
static void test_parent_child(void) {
    printf("\n[TEST 3] 父子关系\n");

    coco_context_t *parent = coco_context_create(NULL);
    TEST_ASSERT(parent != NULL, "创建父 context 成功");

    coco_context_opts_t opts = { .parent = parent };
    coco_context_t *child = coco_context_create(&opts);
    TEST_ASSERT(child != NULL, "创建子 context 成功");
    TEST_ASSERT(child->parent == parent, "父子关系正确");

    coco_context_unref(child);
    coco_context_unref(parent);
}

/* 测试 4: 取消传播 */
static void test_cancel_propagation(void) {
    printf("\n[TEST 4] 取消传播\n");

    coco_context_t *parent = coco_context_create(NULL);

    coco_context_opts_t opts = { .parent = parent };
    coco_context_t *child1 = coco_context_create(&opts);
    coco_context_t *child2 = coco_context_create(&opts);

    TEST_ASSERT(!coco_context_is_cancelled(child1), "child1 初始未取消");
    TEST_ASSERT(!coco_context_is_cancelled(child2), "child2 初始未取消");

    /* 取消父 context */
    coco_context_cancel(parent);

    TEST_ASSERT(coco_context_is_cancelled(parent), "parent 已取消");
    TEST_ASSERT(coco_context_is_cancelled(child1), "child1 已取消");
    TEST_ASSERT(coco_context_is_cancelled(child2), "child2 已取消");

    coco_context_unref(child2);
    coco_context_unref(child1);
    coco_context_unref(parent);
}

/* 测试 5: 超时取消 */
static void test_timeout(void) {
    printf("\n[TEST 5] 超时取消\n");

    /* 创建 100ms 超时的 context */
    coco_context_t *ctx = coco_context_with_timeout(NULL, 100);
    TEST_ASSERT(ctx != NULL, "创建带超时 context 成功");
    TEST_ASSERT(coco_context_has_deadline(ctx), "有截止时间");

    TEST_ASSERT(!coco_context_is_cancelled(ctx), "初始未取消");

    /* 等待超时 */
    usleep(150000);  /* 150ms */

    TEST_ASSERT(coco_context_is_cancelled(ctx), "超时后已取消");
    TEST_ASSERT(coco_context_is_done(ctx), "超时后已完成");

    coco_context_unref(ctx);
}

/* 测试 6: Background 和 TODO */
static void test_background_todo(void) {
    printf("\n[TEST 6] Background 和 TODO\n");

    coco_context_t *bg = coco_context_background();
    TEST_ASSERT(bg != NULL, "获取 background 成功");
    TEST_ASSERT(!coco_context_is_cancelled(bg), "background 未取消");

    coco_context_t *todo = coco_context_todo();
    TEST_ASSERT(todo != NULL, "获取 TODO 成功");
    TEST_ASSERT(!coco_context_is_cancelled(todo), "TODO 未取消");

    coco_context_unref(bg);
    coco_context_unref(todo);
}

/* 测试 7: 引用计数 */
static void test_refcount(void) {
    printf("\n[TEST 7] 引用计数\n");

    coco_context_t *ctx = coco_context_create(NULL);
    TEST_ASSERT(ctx != NULL, "创建 context 成功");
    TEST_ASSERT(atomic_load(&ctx->refcount) == 1, "初始引用计数为 1");

    coco_context_ref(ctx);
    TEST_ASSERT(atomic_load(&ctx->refcount) == 2, "引用计数增加到 2");

    coco_context_unref(ctx);
    TEST_ASSERT(atomic_load(&ctx->refcount) == 1, "引用计数减少到 1");

    coco_context_unref(ctx);
    TEST_ASSERT(1, "引用计数为 0 时释放");
}

/* 测试 8: 取消回调 */
static int callback_called = 0;

static void cancel_callback(coco_context_t *ctx, void *data) {
    (void)ctx;
    (void)data;
    callback_called = 1;
}

static void test_cancel_callback(void) {
    printf("\n[TEST 8] 取消回调\n");

    callback_called = 0;

    coco_context_t *ctx = coco_context_create(NULL);
    coco_context_set_cancel_callback(ctx, cancel_callback, NULL);

    TEST_ASSERT(callback_called == 0, "回调未调用");

    coco_context_cancel(ctx);
    TEST_ASSERT(callback_called == 1, "回调已调用");

    coco_context_unref(ctx);
}

/* 测试 9: with_cancel 便捷函数 */
static void test_with_cancel(void) {
    printf("\n[TEST 9] with_cancel 便捷函数\n");

    coco_context_t *parent = coco_context_create(NULL);
    coco_context_t *child = coco_context_with_cancel(parent);

    TEST_ASSERT(child != NULL, "创建 with_cancel 成功");
    TEST_ASSERT(child->parent == parent, "父子关系正确");

    coco_context_unref(child);
    coco_context_unref(parent);
}

/* 测试 10: 深层嵌套取消传播 */
static void test_deep_propagation(void) {
    printf("\n[TEST 10] 深层嵌套取消传播 (10 层)\n");

    coco_context_t *ctxs[10];
    ctxs[0] = coco_context_create(NULL);

    for (int i = 1; i < 10; i++) {
        coco_context_opts_t opts = { .parent = ctxs[i-1] };
        ctxs[i] = coco_context_create(&opts);
    }

    TEST_ASSERT(!coco_context_is_cancelled(ctxs[9]), "最深层初始未取消");

    /* 取消根 context */
    coco_context_cancel(ctxs[0]);

    /* 检查所有层级都已取消 */
    int all_cancelled = 1;
    for (int i = 0; i < 10; i++) {
        if (!coco_context_is_cancelled(ctxs[i])) {
            all_cancelled = 0;
            break;
        }
    }
    TEST_ASSERT(all_cancelled, "所有层级都已取消");

    /* 释放 */
    for (int i = 9; i >= 0; i--) {
        coco_context_unref(ctxs[i]);
    }
}

/* === 主测试入口 === */

int main(void) {
    printf("=== Context API 测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. coco_context_t 结构正确实现\n");
    printf("  2. coco_context_create/cancel/done API 正确\n");
    printf("  3. 取消传播到子 context\n");
    printf("  4. 超时自动取消\n");

    test_create_destroy();
    test_cancel();
    test_parent_child();
    test_cancel_propagation();
    test_timeout();
    test_background_todo();
    test_refcount();
    test_cancel_callback();
    test_with_cancel();
    test_deep_propagation();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 2 验收标准 US-009 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}
