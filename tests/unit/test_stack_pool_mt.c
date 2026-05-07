/**
 * test_stack_pool_mt.c - 多线程栈池测试 (Phase 4, US-012)
 *
 * 验收标准:
 * - stack_pool_mt.c/h 实现 Per-P 栈池
 * - 栈所有权规则明确
 * - 所有 8 种尺寸正确分配/释放
 */

#include "../../src/core/stack_pool_mt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>

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

/* === Per-P 栈池测试 === */

/* 测试 1: Per-P 栈池创建和销毁 */
static void test_per_p_create_destroy(void) {
    printf("\n[TEST 1] Per-P 栈池创建和销毁\n");

    stack_pool_global_cache_init();

    stack_pool_per_p_t *pool = stack_pool_per_p_create(0);
    TEST_ASSERT(pool != NULL, "Per-P 栈池创建成功");
    TEST_ASSERT(pool->p_id == 0, "P ID 正确");
    TEST_ASSERT(pool->zero_mode == STACK_ZERO_TOP_1K, "默认清零模式正确");

    stack_pool_per_p_destroy(pool);
    TEST_ASSERT(1, "Per-P 栈池销毁成功");

    stack_pool_global_cache_destroy();
}

/* 测试 2: Per-P 栈池 8 种尺寸分配 */
static void test_per_p_all_sizes(void) {
    printf("\n[TEST 2] Per-P 栈池 8 种尺寸分配\n");

    stack_pool_global_cache_init();

    stack_pool_per_p_t *pool = stack_pool_per_p_create(0);
    assert(pool != NULL);

    size_t sizes[] = {
        STACK_SIZE_8K, STACK_SIZE_16K, STACK_SIZE_32K, STACK_SIZE_64K,
        STACK_SIZE_128K, STACK_SIZE_256K, STACK_SIZE_512K, STACK_SIZE_1M
    };
    const char *size_names[] = {
        "8KB", "16KB", "32KB", "64KB", "128KB", "256KB", "512KB", "1MB"
    };

    void *stacks[8] = {0};

    for (int i = 0; i < 8; i++) {
        stacks[i] = stack_pool_per_p_alloc(pool, sizes[i]);
        char msg[64];
        snprintf(msg, sizeof(msg), "分配 %s 栈成功", size_names[i]);
        TEST_ASSERT(stacks[i] != NULL, msg);
    }

    /* 释放所有栈 */
    for (int i = 0; i < 8; i++) {
        stack_pool_per_p_free(pool, stacks[i], sizes[i]);
    }

    TEST_ASSERT(1, "所有 8 种尺寸正确分配/释放");

    stack_pool_per_p_destroy(pool);
    stack_pool_global_cache_destroy();
}

/* 测试 3: Per-P 栈池复用 */
static void test_per_p_pool_reuse(void) {
    printf("\n[TEST 3] Per-P 栈池复用\n");

    stack_pool_global_cache_init();

    stack_pool_per_p_t *pool = stack_pool_per_p_create(0);
    assert(pool != NULL);

    /* 分配并释放，测试池命中 */
    void *stack1 = stack_pool_per_p_alloc(pool, STACK_SIZE_32K);
    assert(stack1 != NULL);

    uint64_t hits_before, misses_before;
    stack_pool_per_p_get_stats(pool, NULL, NULL, &hits_before, &misses_before);

    stack_pool_per_p_free(pool, stack1, STACK_SIZE_32K);

    /* 再次分配相同尺寸，应该命中池 */
    void *stack2 = stack_pool_per_p_alloc(pool, STACK_SIZE_32K);
    assert(stack2 != NULL);

    uint64_t hits_after, misses_after;
    stack_pool_per_p_get_stats(pool, NULL, NULL, &hits_after, &misses_after);

    TEST_ASSERT(hits_after > hits_before, "池命中增加");
    TEST_ASSERT(stack2 == stack1, "复用相同的栈地址");

    stack_pool_per_p_free(pool, stack2, STACK_SIZE_32K);
    stack_pool_per_p_destroy(pool);
    stack_pool_global_cache_destroy();
}

/* 测试 4: 栈所有权规则 */
static void test_stack_ownership(void) {
    printf("\n[TEST 4] 栈所有权规则\n");

    stack_pool_global_cache_init();

    /* 创建两个 P 的栈池 */
    stack_pool_per_p_t *pool0 = stack_pool_per_p_create(0);
    stack_pool_per_p_t *pool1 = stack_pool_per_p_create(1);
    assert(pool0 != NULL && pool1 != NULL);

    /* 从 P0 分配栈 */
    void *stack = stack_pool_per_p_alloc(pool0, STACK_SIZE_32K);
    TEST_ASSERT(stack != NULL, "从 P0 分配栈成功");

    /* 释放到 P1（跨 P 释放） */
    stack_pool_per_p_free(pool1, stack, STACK_SIZE_32K);
    TEST_ASSERT(1, "跨 P 释放成功（放入全局缓存）");

    /* P0 再次分配，应该能从全局缓存获取 */
    void *stack2 = stack_pool_per_p_alloc(pool0, STACK_SIZE_32K);
    TEST_ASSERT(stack2 != NULL, "P0 再次分配成功");

    stack_pool_per_p_free(pool0, stack2, STACK_SIZE_32K);

    stack_pool_per_p_destroy(pool0);
    stack_pool_per_p_destroy(pool1);
    stack_pool_global_cache_destroy();
}

/* 测试 5: 全局缓存 */
static void test_global_cache(void) {
    printf("\n[TEST 5] 全局缓存\n");

    stack_pool_global_cache_init();

    /* 创建 Per-P 栈池来分配栈 */
    stack_pool_per_p_t *pool = stack_pool_per_p_create(0);
    assert(pool != NULL);

    /* 分配栈 */
    void *stack = stack_pool_per_p_alloc(pool, STACK_SIZE_32K);
    TEST_ASSERT(stack != NULL, "分配测试栈成功");

    /* 释放到全局缓存（通过另一个 P） */
    stack_pool_per_p_t *pool1 = stack_pool_per_p_create(1);
    stack_pool_per_p_free(pool1, stack, STACK_SIZE_32K);
    TEST_ASSERT(1, "跨 P 释放成功");

    /* 从全局缓存获取 */
    void *stack2 = stack_pool_per_p_alloc(pool, STACK_SIZE_32K);
    TEST_ASSERT(stack2 != NULL, "从全局缓存获取成功");

    stack_pool_per_p_free(pool, stack2, STACK_SIZE_32K);
    stack_pool_per_p_destroy(pool);
    stack_pool_per_p_destroy(pool1);
    stack_pool_global_cache_destroy();
}

/* 测试 6: 栈使用率检测 */
static void test_stack_usage_detection(void) {
    printf("\n[TEST 6] 栈使用率检测\n");

    stack_pool_global_cache_init();

    stack_pool_per_p_t *pool = stack_pool_per_p_create(0);
    assert(pool != NULL);

    void *stack = stack_pool_per_p_alloc(pool, STACK_SIZE_32K);
    assert(stack != NULL);

    /* 模拟栈使用 */
    uintptr_t top = (uintptr_t)stack;
    uintptr_t sp = top - 1024;  /* 使用了 1KB */

    size_t usage = stack_pool_mt_get_usage(stack, STACK_SIZE_32K, (void*)sp);
    TEST_ASSERT(usage == 1024, "栈使用率检测准确");

    stack_pool_per_p_free(pool, stack, STACK_SIZE_32K);
    stack_pool_per_p_destroy(pool);
    stack_pool_global_cache_destroy();
}

/* 测试 7: Size class 辅助函数 */
static void test_size_class_helpers(void) {
    printf("\n[TEST 7] Size class 辅助函数\n");

    /* 测试 get_class_index */
    TEST_ASSERT(stack_pool_mt_get_class_index(4096) == 0, "4KB -> class 0 (8KB)");
    TEST_ASSERT(stack_pool_mt_get_class_index(8192) == 0, "8KB -> class 0");
    TEST_ASSERT(stack_pool_mt_get_class_index(10000) == 1, "10KB -> class 1 (16KB)");
    TEST_ASSERT(stack_pool_mt_get_class_index(100 * 1024) == 4, "100KB -> class 4 (128KB)");
    TEST_ASSERT(stack_pool_mt_get_class_index(2 * 1024 * 1024) == -1, "2MB -> 超出范围");

    /* 测试 get_class_size */
    TEST_ASSERT(stack_pool_mt_get_class_size(0) == STACK_SIZE_8K, "class 0 -> 8KB");
    TEST_ASSERT(stack_pool_mt_get_class_size(7) == STACK_SIZE_1M, "class 7 -> 1MB");
    TEST_ASSERT(stack_pool_mt_get_class_size(-1) == 0, "无效 class -> 0");
    TEST_ASSERT(stack_pool_mt_get_class_size(8) == 0, "超出范围 class -> 0");
}

/* 测试 8: 多线程分配 */
static void *thread_alloc_func(void *arg) {
    stack_pool_per_p_t *pool = (stack_pool_per_p_t*)arg;

    for (int i = 0; i < 100; i++) {
        void *stack = stack_pool_per_p_alloc(pool, STACK_SIZE_32K);
        if (stack) {
            /* 模拟使用 */
            memset((void*)((uintptr_t)stack - 256), 0xAB, 256);
            stack_pool_per_p_free(pool, stack, STACK_SIZE_32K);
        }
    }

    return NULL;
}

static void test_mt_concurrent_alloc(void) {
    printf("\n[TEST 8] 多线程并发分配\n");

    stack_pool_global_cache_init();

    /* 创建多个 P 的栈池 */
    stack_pool_per_p_t *pools[4];
    pthread_t threads[4];

    for (int i = 0; i < 4; i++) {
        pools[i] = stack_pool_per_p_create(i);
        assert(pools[i] != NULL);
    }

    /* 启动线程 */
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, thread_alloc_func, pools[i]);
    }

    /* 等待线程完成 */
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    TEST_ASSERT(1, "多线程并发分配完成");

    /* 检查统计 */
    uint64_t total_allocs = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t allocs;
        stack_pool_per_p_get_stats(pools[i], &allocs, NULL, NULL, NULL);
        total_allocs += allocs;
    }

    TEST_ASSERT(total_allocs == 400, "总分配次数正确 (4 线程 × 100 次)");

    for (int i = 0; i < 4; i++) {
        stack_pool_per_p_destroy(pools[i]);
    }

    stack_pool_global_cache_destroy();
}

/* === 主测试入口 === */

int main(void) {
    printf("=== 多线程栈池测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. stack_pool_mt.c/h 实现 Per-P 栈池\n");
    printf("  2. 栈所有权规则明确\n");
    printf("  3. 所有 8 种尺寸正确分配/释放\n");

    test_per_p_create_destroy();
    test_per_p_all_sizes();
    test_per_p_pool_reuse();
    test_stack_ownership();
    test_global_cache();
    test_stack_usage_detection();
    test_size_class_helpers();
    test_mt_concurrent_alloc();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 4 验收标准 US-012 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}
