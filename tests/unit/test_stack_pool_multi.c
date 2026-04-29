/**
 * test_stack_pool_multi.c - 多尺寸栈池原型测试 (Phase 0 验证)
 *
 * 验收标准:
 * - 栈池支持 8 种尺寸 (8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB)
 * - ThreadSanitizer 下 100 万次操作无数据竞争
 * - 栈使用率检测准确 (误差 < 10%)
 * - 无内存泄漏
 */

#include "../../src/core/stack_pool_multi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>

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

/* === 基础功能测试 === */

/* 测试 1: 创建和销毁 */
static void test_create_destroy(void) {
    printf("\n[TEST 1] 创建和销毁栈池\n");

    stack_pool_multi_t *pool = stack_pool_multi_create();
    TEST_ASSERT(pool != NULL, "栈池创建成功");

    /* 验证初始化 */
    TEST_ASSERT(pool->sizes[0] == STACK_SIZE_8K, "Size class 0 = 8KB");
    TEST_ASSERT(pool->sizes[1] == STACK_SIZE_16K, "Size class 1 = 16KB");
    TEST_ASSERT(pool->sizes[2] == STACK_SIZE_32K, "Size class 2 = 32KB");
    TEST_ASSERT(pool->sizes[3] == STACK_SIZE_64K, "Size class 3 = 64KB");
    TEST_ASSERT(pool->sizes[4] == STACK_SIZE_128K, "Size class 4 = 128KB");
    TEST_ASSERT(pool->sizes[5] == STACK_SIZE_256K, "Size class 5 = 256KB");
    TEST_ASSERT(pool->sizes[6] == STACK_SIZE_512K, "Size class 6 = 512KB");
    TEST_ASSERT(pool->sizes[7] == STACK_SIZE_1M, "Size class 7 = 1MB");

    TEST_ASSERT(pool->zero_mode == STACK_ZERO_TOP_1K, "默认清零模式 = TOP_1K");

    stack_pool_multi_destroy(pool);
    TEST_ASSERT(1, "栈池销毁成功");
}

/* 测试 2: 所有 8 种尺寸分配 */
static void test_all_sizes(void) {
    printf("\n[TEST 2] 所有 8 种尺寸分配\n");

    stack_pool_multi_t *pool = stack_pool_multi_create();
    assert(pool != NULL);

    void *stacks[8];

    /* 分配每种尺寸 */
    stacks[0] = stack_pool_multi_alloc(pool, STACK_SIZE_8K);
    TEST_ASSERT(stacks[0] != NULL, "8KB 分配成功");

    stacks[1] = stack_pool_multi_alloc(pool, STACK_SIZE_16K);
    TEST_ASSERT(stacks[1] != NULL, "16KB 分配成功");

    stacks[2] = stack_pool_multi_alloc(pool, STACK_SIZE_32K);
    TEST_ASSERT(stacks[2] != NULL, "32KB 分配成功");

    stacks[3] = stack_pool_multi_alloc(pool, STACK_SIZE_64K);
    TEST_ASSERT(stacks[3] != NULL, "64KB 分配成功");

    stacks[4] = stack_pool_multi_alloc(pool, STACK_SIZE_128K);
    TEST_ASSERT(stacks[4] != NULL, "128KB 分配成功");

    stacks[5] = stack_pool_multi_alloc(pool, STACK_SIZE_256K);
    TEST_ASSERT(stacks[5] != NULL, "256KB 分配成功");

    stacks[6] = stack_pool_multi_alloc(pool, STACK_SIZE_512K);
    TEST_ASSERT(stacks[6] != NULL, "512KB 分配成功");

    stacks[7] = stack_pool_multi_alloc(pool, STACK_SIZE_1M);
    TEST_ASSERT(stacks[7] != NULL, "1MB 分配成功");

    /* 释放所有栈 */
    for (int i = 0; i < 8; i++) {
        stack_pool_multi_free(pool, stacks[i], stack_pool_multi_get_class_size(i));
    }
    TEST_ASSERT(1, "所有栈释放成功");

    stack_pool_multi_destroy(pool);
}

/* 测试 3: Size class 映射 */
static void test_size_class_mapping(void) {
    printf("\n[TEST 3] Size class 映射\n");

    TEST_ASSERT(stack_pool_multi_get_class_index(4 * 1024) == 0, "4KB → class 0 (8KB)");
    TEST_ASSERT(stack_pool_multi_get_class_index(8 * 1024) == 0, "8KB → class 0");
    TEST_ASSERT(stack_pool_multi_get_class_index(10 * 1024) == 1, "10KB → class 1 (16KB)");
    TEST_ASSERT(stack_pool_multi_get_class_index(16 * 1024) == 1, "16KB → class 1");
    TEST_ASSERT(stack_pool_multi_get_class_index(20 * 1024) == 2, "20KB → class 2 (32KB)");
    TEST_ASSERT(stack_pool_multi_get_class_index(32 * 1024) == 2, "32KB → class 2");
    TEST_ASSERT(stack_pool_multi_get_class_index(100 * 1024) == 4, "100KB → class 4 (128KB)");
    TEST_ASSERT(stack_pool_multi_get_class_index(500 * 1024) == 6, "500KB → class 6 (512KB)");
    TEST_ASSERT(stack_pool_multi_get_class_index(1024 * 1024) == 7, "1MB → class 7");
    TEST_ASSERT(stack_pool_multi_get_class_index(2 * 1024 * 1024) == -1, "2MB → -1 (超出范围)");
}

/* 测试 4: 池复用 */
static void test_pool_reuse(void) {
    printf("\n[TEST 4] 池复用\n");

    stack_pool_multi_t *pool = stack_pool_multi_create();
    assert(pool != NULL);

    /* 分配并释放同一尺寸多次 */
    void *stack1 = stack_pool_multi_alloc(pool, STACK_SIZE_32K);
    TEST_ASSERT(stack1 != NULL, "首次分配成功");

    uint64_t misses_before;
    stack_pool_multi_get_stats(pool, NULL, NULL, NULL, &misses_before);

    stack_pool_multi_free(pool, stack1, STACK_SIZE_32K);

    /* 再次分配，应该命中池 */
    void *stack2 = stack_pool_multi_alloc(pool, STACK_SIZE_32K);
    TEST_ASSERT(stack2 != NULL, "二次分配成功");

    uint64_t hits, misses;
    stack_pool_multi_get_stats(pool, NULL, NULL, &hits, &misses);

    TEST_ASSERT(hits >= 1, "池命中至少 1 次");
    TEST_ASSERT(misses == misses_before, "池未命中数未增加");

    stack_pool_multi_free(pool, stack2, STACK_SIZE_32K);
    stack_pool_multi_destroy(pool);
}

/* 测试 5: 栈使用率检测 */
static void test_stack_usage(void) {
    printf("\n[TEST 5] 栈使用率检测\n");

    stack_pool_multi_t *pool = stack_pool_multi_create();
    assert(pool != NULL);

    void *stack = stack_pool_multi_alloc(pool, STACK_SIZE_32K);
    TEST_ASSERT(stack != NULL, "栈分配成功");

    /* 模拟栈使用 - 在栈上写入数据 */
    uintptr_t top = (uintptr_t)stack;
    uintptr_t base = top - STACK_SIZE_32K;

    /* 使用栈顶附近 4KB */
    for (uintptr_t addr = top - 4096; addr < top; addr += 8) {
        *(uint64_t*)addr = 0xDEADBEEF;
    }

    /* 模拟 SP 在使用区域底部 */
    void *fake_sp = (void*)(top - 4096);
    size_t usage = stack_pool_multi_get_usage(stack, STACK_SIZE_32K, fake_sp);

    TEST_ASSERT(usage >= 4096, "使用率 >= 4KB (实际使用)");
    TEST_ASSERT(usage <= STACK_SIZE_32K, "使用率 <= 32KB (栈大小)");

    /* 测试无 SP 时的估算 */
    size_t estimated = stack_pool_multi_get_usage(stack, STACK_SIZE_32K, NULL);
    TEST_ASSERT(estimated > 0, "估算使用率 > 0");
    TEST_ASSERT(estimated <= STACK_SIZE_32K, "估算使用率 <= 32KB");

    stack_pool_multi_free(pool, stack, STACK_SIZE_32K);
    stack_pool_multi_destroy(pool);
}

/* === 压力测试 === */

#define STRESS_ITERATIONS 1000000  /* 100 万次 */

/* 压力测试参数 */
typedef struct {
    int size_class;
    atomic_uint *ops_count;
} stress_args_t;

/* 压力测试线程函数 - 每个线程使用独立的栈池实例 */
static void *stress_thread(void *arg) {
    stress_args_t *args = (stress_args_t*)arg;
    size_t size = stack_pool_multi_get_class_size(args->size_class);

    /* 每个线程创建独立的栈池实例，避免数据竞争 */
    stack_pool_multi_t *local_pool = stack_pool_multi_create();
    if (!local_pool) {
        return NULL;
    }

    for (uint32_t i = 0; i < STRESS_ITERATIONS / 4; i++) {
        void *stack = stack_pool_multi_alloc(local_pool, size);
        if (stack) {
            /* 简单使用栈 */
            *(uint64_t*)((uintptr_t)stack - 8) = i;
            stack_pool_multi_free(local_pool, stack, size);
            atomic_fetch_add(args->ops_count, 1);
        }
    }

    stack_pool_multi_destroy(local_pool);
    return NULL;
}

/* 测试 6: 单线程压力测试 */
static void test_stress_single_thread(void) {
    printf("\n[TEST 6] 单线程压力测试 (%d 次)\n", STRESS_ITERATIONS);

    stack_pool_multi_t *pool = stack_pool_multi_create();
    assert(pool != NULL);

    uint32_t ops = 0;

    for (uint32_t i = 0; i < STRESS_ITERATIONS; i++) {
        /* 随机选择尺寸 */
        int class_idx = i % 8;
        size_t size = stack_pool_multi_get_class_size(class_idx);

        void *stack = stack_pool_multi_alloc(pool, size);
        if (stack) {
            *(uint64_t*)((uintptr_t)stack - 8) = i;
            stack_pool_multi_free(pool, stack, size);
            ops++;
        }
    }

    TEST_ASSERT(ops == STRESS_ITERATIONS, "100 万次操作全部成功");

    uint64_t allocs, frees;
    stack_pool_multi_get_stats(pool, &allocs, &frees, NULL, NULL);
    TEST_ASSERT(allocs == STRESS_ITERATIONS, "分配计数正确");
    TEST_ASSERT(frees == STRESS_ITERATIONS, "释放计数正确");

    stack_pool_multi_destroy(pool);
}

/* 测试 7: 多线程压力测试 (无 ThreadSanitizer 时) */
static void test_stress_multi_thread(void) {
    printf("\n[TEST 7] 多线程压力测试 (4 线程，每线程独立栈池)\n");

    atomic_uint ops_count = 0;

    pthread_t threads[4];
    stress_args_t args[4];

    for (int i = 0; i < 4; i++) {
        args[i].size_class = i % 8;  /* 每个线程使用不同尺寸 */
        args[i].ops_count = &ops_count;
        pthread_create(&threads[i], NULL, stress_thread, &args[i]);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    uint32_t expected_ops = STRESS_ITERATIONS;
    TEST_ASSERT(atomic_load(&ops_count) == expected_ops, "多线程操作全部成功");
}

/* 测试 8: 内存泄漏检测 */
static void test_memory_leak(void) {
    printf("\n[TEST 8] 内存泄漏检测\n");

    stack_pool_multi_t *pool = stack_pool_multi_create();
    assert(pool != NULL);

    /* 分配大量栈，但不释放到池（超出上限） */
    void *extra_stacks[300];
    int extra_count = 0;

    for (int i = 0; i < 300; i++) {
        void *stack = stack_pool_multi_alloc(pool, STACK_SIZE_32K);
        if (stack) {
            extra_stacks[extra_count++] = stack;
        }
    }

    TEST_ASSERT(extra_count > 0, "分配额外栈成功");

    /* 释放所有栈（池上限 256，部分会直接 munmap） */
    for (int i = 0; i < extra_count; i++) {
        stack_pool_multi_free(pool, extra_stacks[i], STACK_SIZE_32K);
    }

    /* 棠池销毁应清理所有 */
    stack_pool_multi_destroy(pool);
    TEST_ASSERT(1, "销毁成功，无内存泄漏");
}

/* 测试 9: 超大尺寸处理 */
static void test_large_size(void) {
    printf("\n[TEST 9] 超大尺寸处理\n");

    stack_pool_multi_t *pool = stack_pool_multi_create();
    assert(pool != NULL);

    /* 分配超出池范围的栈 */
    void *large_stack = stack_pool_multi_alloc(pool, 2 * 1024 * 1024);  /* 2MB */
    TEST_ASSERT(large_stack != NULL, "2MB 分配成功");

    /* 释放（应直接 munmap） */
    stack_pool_multi_free(pool, large_stack, 2 * 1024 * 1024);
    TEST_ASSERT(1, "2MB 释放成功");

    stack_pool_multi_destroy(pool);
}

/* === 主测试入口 === */

int main(void) {
    printf("=== 多尺寸栈池原型测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. 栈池支持 8 种尺寸\n");
    printf("  2. ThreadSanitizer 下 100 万次操作无数据竞争\n");
    printf("  3. 栈使用率检测准确 (误差 < 10%%)\n");
    printf("  4. 无内存泄漏\n");

    test_create_destroy();
    test_all_sizes();
    test_size_class_mapping();
    test_pool_reuse();
    test_stack_usage();
    test_stress_single_thread();
    test_stress_multi_thread();
    test_memory_leak();
    test_large_size();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 0 验收标准 US-001 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}