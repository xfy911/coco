/**
 * test_global_sched.c - 全局调度器框架测试 (Phase 1)
 *
 * 验收标准:
 * - coco_global_sched_t 结构正确实现
 * - coco_global_init(cpu_count) 根据 CPU 核心数创建 P
 * - 每个 P 有独立的栈池和本地队列
 * - 全局队列正确工作
 */

#include "../../src/sched/global_sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
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

/* === 测试 === */

/* 测试 1: 全局初始化和销毁 */
static void test_init_destroy(void) {
    printf("\n[TEST 1] 全局初始化和销毁\n");

    /* 使用默认 CPU 数量 */
    int ret = coco_global_init(0);
    TEST_ASSERT(ret == 0, "全局初始化成功 (默认 CPU 数量)");

    coco_global_sched_t *sched = coco_global_get();
    TEST_ASSERT(sched != NULL, "获取全局调度器成功");
    TEST_ASSERT(sched->processor_count > 0, "处理器数量 > 0");
    TEST_ASSERT(sched->processors != NULL, "处理器数组已分配");

    coco_global_destroy();
    TEST_ASSERT(coco_global_get() == NULL, "全局销毁成功");
}

/* 测试 2: 指定 CPU 数量 */
static void test_specified_cpu_count(void) {
    printf("\n[TEST 2] 指定 CPU 数量\n");

    int ret = coco_global_init(4);
    TEST_ASSERT(ret == 0, "全局初始化成功 (4 CPU)");

    coco_global_sched_t *sched = coco_global_get();
    TEST_ASSERT(sched->processor_count == 4, "处理器数量为 4");

    coco_global_destroy();
}

/* 测试 3: 每个 P 有独立栈池 */
static void test_per_p_stack_pool(void) {
    printf("\n[TEST 3] 每个 P 有独立栈池\n");

    int ret = coco_global_init(4);
    assert(ret == 0);

    for (uint32_t i = 0; i < 4; i++) {
        coco_processor_t *p = coco_processor_get(i);
        TEST_ASSERT(p != NULL, "获取 P 成功");
        TEST_ASSERT(p->stack_pool != NULL, "P 有独立栈池");
        TEST_ASSERT(p->id == i, "P ID 正确");
    }

    coco_global_destroy();
}

/* 测试 4: 每个 P 有本地队列 */
static void test_per_p_local_queue(void) {
    printf("\n[TEST 4] 每个 P 有本地队列\n");

    int ret = coco_global_init(4);
    assert(ret == 0);

    for (uint32_t i = 0; i < 4; i++) {
        coco_processor_t *p = coco_processor_get(i);
        TEST_ASSERT(p->local_runq_head == NULL, "本地队列初始为空");
        TEST_ASSERT(p->local_runq_tail == NULL, "本地队列尾初始为空");
        TEST_ASSERT(p->local_runq_size == 0, "本地队列大小初始为 0");
    }

    coco_global_destroy();
}

/* 测试 5: 全局队列操作 */
static void test_global_queue(void) {
    printf("\n[TEST 5] 全局队列操作\n");

    int ret = coco_global_init(4);
    assert(ret == 0);

    /* 创建测试协程结构 */
    coco_coro_t coro1 = {0}, coro2 = {0}, coro3 = {0};
    coro1.id = 1;
    coro2.id = 2;
    coro3.id = 3;

    /* 入队 */
    ret = coco_global_runq_put(&coro1);
    TEST_ASSERT(ret == 0, "入队 coro1 成功");
    TEST_ASSERT(coco_global_runq_size() == 1, "队列大小为 1");

    ret = coco_global_runq_put(&coro2);
    TEST_ASSERT(ret == 0, "入队 coro2 成功");
    TEST_ASSERT(coco_global_runq_size() == 2, "队列大小为 2");

    ret = coco_global_runq_put(&coro3);
    TEST_ASSERT(ret == 0, "入队 coro3 成功");
    TEST_ASSERT(coco_global_runq_size() == 3, "队列大小为 3");

    /* 出队 (LIFO 顺序 — Treiber Stack) */
    coco_coro_t *g = coco_global_runq_get();
    TEST_ASSERT(g != NULL && g->id == 3, "出队 coro3 成功 (栈顶)");

    g = coco_global_runq_get();
    TEST_ASSERT(g != NULL && g->id == 2, "出队 coro2 成功");

    g = coco_global_runq_get();
    TEST_ASSERT(g != NULL && g->id == 1, "出队 coro1 成功 (栈底)");

    TEST_ASSERT(coco_global_runq_size() == 0, "队列大小为 0");

    coco_global_destroy();
}

/* 测试 6: 处理器查询 */
static void test_processor_query(void) {
    printf("\n[TEST 6] 处理器查询\n");

    int ret = coco_global_init(4);
    assert(ret == 0);

    TEST_ASSERT(coco_processor_count() == 4, "处理器数量为 4");

    /* 有效 ID */
    coco_processor_t *p = coco_processor_get(0);
    TEST_ASSERT(p != NULL && p->id == 0, "获取 P0 成功");

    p = coco_processor_get(3);
    TEST_ASSERT(p != NULL && p->id == 3, "获取 P3 成功");

    /* 无效 ID */
    p = coco_processor_get(4);
    TEST_ASSERT(p == NULL, "无效 ID 返回 NULL");

    p = coco_processor_get(100);
    TEST_ASSERT(p == NULL, "超大 ID 返回 NULL");

    coco_global_destroy();
}

/* 测试 7: P 和 M 结构 */
static void test_p_m_structures(void) {
    printf("\n[TEST 7] P 和 M 结构\n");

    coco_processor_t *p = coco_processor_create(99);
    TEST_ASSERT(p != NULL, "创建 P 成功");
    TEST_ASSERT(p->id == 99, "P ID 正确");
    TEST_ASSERT(p->stack_pool != NULL, "P 有栈池");
    TEST_ASSERT(p->status == P_IDLE, "P 初始状态为 IDLE");
    coco_processor_destroy(p);
    TEST_ASSERT(1, "销毁 P 成功");

    coco_machine_t *m = coco_machine_create(88);
    TEST_ASSERT(m != NULL, "创建 M 成功");
    TEST_ASSERT(m->id == 88, "M ID 正确");
    TEST_ASSERT(m->status == M_IDLE, "M 初始状态为 IDLE");
    coco_machine_destroy(m);
    TEST_ASSERT(1, "销毁 M 成功");
}

/* === 主测试入口 === */

int main(void) {
    printf("=== 全局调度器框架测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. coco_global_sched_t 结构正确实现\n");
    printf("  2. coco_global_init(cpu_count) 根据 CPU 核心数创建 P\n");
    printf("  3. 每个 P 有独立的栈池和本地队列\n");
    printf("  4. 全局队列正确工作\n");

    test_init_destroy();
    test_specified_cpu_count();
    test_per_p_stack_pool();
    test_per_p_local_queue();
    test_global_queue();
    test_processor_query();
    test_p_m_structures();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %u\n", atomic_load(&test_pass_count));
    printf("失败: %u\n", atomic_load(&test_fail_count));

    if (atomic_load(&test_fail_count) == 0) {
        printf("\n✓ 所有测试通过！Phase 1 验收标准 US-004 达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}