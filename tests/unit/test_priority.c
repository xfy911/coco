/**
 * test_priority.c - 优先级 API 测试
 *
 * 测试协程优先级设置和调度
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/coco_internal.h"

/* 测试统计 */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL - %s\n", msg); \
    tests_failed++; \
} while(0)

/* ========== 测试数据 ========== */

static int exec_order[4] = {0};
static int exec_count = 0;

/* ========== 测试用例 ========== */

static void simple_coro(void *arg) {
    (void)arg;
    coco_yield();
}

/**
 * test_set_priority_valid - 验证设置有效优先级
 */
TEST(test_set_priority_valid) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    coco_coro_t *coro = coco_create(sched, simple_coro, NULL, 0);
    if (!coro) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutine");
        return;
    }

    /* 测试所有有效优先级 */
    coco_set_priority(coro, COCO_PRIORITY_HIGH);
    if (coco_get_priority(coro) != COCO_PRIORITY_HIGH) {
        coco_sched_destroy(sched);
        FAIL("HIGH priority not set");
        return;
    }

    coco_set_priority(coro, COCO_PRIORITY_NORMAL);
    if (coco_get_priority(coro) != COCO_PRIORITY_NORMAL) {
        coco_sched_destroy(sched);
        FAIL("NORMAL priority not set");
        return;
    }

    coco_set_priority(coro, COCO_PRIORITY_LOW);
    if (coco_get_priority(coro) != COCO_PRIORITY_LOW) {
        coco_sched_destroy(sched);
        FAIL("LOW priority not set");
        return;
    }

    coco_set_priority(coro, COCO_PRIORITY_IDLE);
    if (coco_get_priority(coro) != COCO_PRIORITY_IDLE) {
        coco_sched_destroy(sched);
        FAIL("IDLE priority not set");
        return;
    }

    /* 运行完成 */
    coco_sched_run(sched);

    PASS();
    coco_sched_destroy(sched);
}

/**
 * test_set_priority_invalid - 验证无效优先级不改变当前值
 */
TEST(test_set_priority_invalid) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    coco_coro_t *coro = coco_create(sched, simple_coro, NULL, 0);
    if (!coro) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutine");
        return;
    }

    /* 设置有效优先级 */
    coco_set_priority(coro, COCO_PRIORITY_HIGH);

    /* 尝试设置无效优先级 (超出范围) */
    coco_set_priority(coro, (coco_priority_t)100);

    /* 应保持原值 */
    if (coco_get_priority(coro) == COCO_PRIORITY_HIGH) {
        coco_sched_run(sched);
        PASS();
    } else {
        FAIL("invalid priority changed value");
    }

    coco_sched_destroy(sched);
}

/**
 * test_get_priority - 验证获取优先级
 */
TEST(test_get_priority) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    coco_coro_t *coro = coco_create(sched, simple_coro, NULL, 0);
    if (!coro) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutine");
        return;
    }

    /* 新创建的协程默认优先级应为 NORMAL */
    coco_priority_t prio = coco_get_priority(coro);
    if (prio == COCO_PRIORITY_NORMAL) {
        coco_sched_run(sched);
        PASS();
    } else {
        FAIL("default priority not NORMAL");
    }

    coco_sched_destroy(sched);
}

/**
 * test_get_priority_null - 验证对 NULL 协程获取优先级返回 NORMAL
 */
TEST(test_get_priority_null) {
    coco_priority_t prio = coco_get_priority(NULL);

    if (prio == COCO_PRIORITY_NORMAL) {
        PASS();
    } else {
        FAIL("NULL coroutine should return NORMAL priority");
    }
}

/**
 * test_priority_scheduling - 验证高优先级协程先调度
 */
static void prio_coro(void *arg) {
    int id = (int)(intptr_t)arg;
    exec_order[exec_count++] = id;
}

TEST(test_priority_scheduling) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    /* 重置计数器 */
    exec_count = 0;
    memset(exec_order, 0, sizeof(exec_order));

    /* 创建三个协程，不同优先级 */
    coco_coro_t *c_low = coco_create(sched, prio_coro, (void*)(intptr_t)1, 0);
    coco_coro_t *c_high = coco_create(sched, prio_coro, (void*)(intptr_t)2, 0);
    coco_coro_t *c_normal = coco_create(sched, prio_coro, (void*)(intptr_t)3, 0);

    if (!c_low || !c_high || !c_normal) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutines");
        return;
    }

    coco_set_priority(c_low, COCO_PRIORITY_LOW);
    coco_set_priority(c_high, COCO_PRIORITY_HIGH);
    coco_set_priority(c_normal, COCO_PRIORITY_NORMAL);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 高优先级应先执行 */
    if (exec_order[0] == 2) {
        PASS();
    } else {
        FAIL("high priority did not execute first");
    }

    coco_sched_destroy(sched);
}

/* ========== 主函数 ========== */

int main(void) {
    printf("=== Priority Tests ===\n");

    RUN_TEST(test_set_priority_valid);
    RUN_TEST(test_set_priority_invalid);
    RUN_TEST(test_get_priority);
    RUN_TEST(test_get_priority_null);
    RUN_TEST(test_priority_scheduling);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
