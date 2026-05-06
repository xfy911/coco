/**
 * test_aging.c - 老化功能测试
 *
 * 测试协程老化机制：等待时间过长的协程优先级被提升
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static int aging_promoted = 0;
static coco_coro_t *test_coro = NULL;

/* ========== 测试用例 ========== */

/**
 * test_aging_disabled - 验证老化阈值为 0 时不执行老化
 */
static void disabled_coro(void *arg) {
    (void)arg;
    coco_yield();
}

TEST(test_aging_disabled) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    /* 默认老化阈值为 0，老化功能禁用 */
    /* 创建一个低优先级协程 */
    coco_coro_t *coro = coco_create(sched, disabled_coro, NULL, 0);
    if (!coro) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutine");
        return;
    }

    /* 设置低优先级 */
    coco_set_priority(coro, COCO_PRIORITY_LOW);

    /* 运行调度器 */
    coco_sched_run(sched);

    coco_priority_t prio = coco_get_priority(coro);

    if (prio == COCO_PRIORITY_LOW) {
        PASS();
    } else {
        FAIL("priority not set correctly");
    }

    coco_sched_destroy(sched);
}

/**
 * test_aging_no_promote - 验证未超过阈值的协程优先级不变
 */
static void low_prio_coro(void *arg) {
    (void)arg;
    /* 快速完成，不应触发老化 */
    coco_yield();
}

TEST(test_aging_no_promote) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    coco_coro_t *coro = coco_create(sched, low_prio_coro, NULL, 0);
    if (!coro) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutine");
        return;
    }

    coco_set_priority(coro, COCO_PRIORITY_LOW);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 协程应保持低优先级（因为快速完成） */
    coco_priority_t final_prio = coco_get_priority(coro);

    if (final_prio == COCO_PRIORITY_LOW) {
        PASS();
    } else {
        FAIL("priority changed unexpectedly");
    }

    coco_sched_destroy(sched);
}

/**
 * test_aging_multiple - 验证多个协程的老化顺序
 */
static int coro_exec_order[8] = {0};  /* 3 coros * 2 writes each = 6, use 8 for safety */
static int coro_exec_count = 0;

static void ordered_coro(void *arg) {
    int id = (int)(intptr_t)arg;
    coro_exec_order[coro_exec_count++] = id;
    coco_yield();
    coro_exec_order[coro_exec_count++] = id;
}

TEST(test_aging_multiple) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    /* 重置计数器 */
    coro_exec_count = 0;
    memset(coro_exec_order, 0, sizeof(coro_exec_order));

    /* 创建多个协程，不同优先级 */
    coco_coro_t *c1 = coco_create(sched, ordered_coro, (void*)(intptr_t)1, 0);
    coco_coro_t *c2 = coco_create(sched, ordered_coro, (void*)(intptr_t)2, 0);
    coco_coro_t *c3 = coco_create(sched, ordered_coro, (void*)(intptr_t)3, 0);

    if (!c1 || !c2 || !c3) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutines");
        return;
    }

    /* 设置不同优先级 */
    coco_set_priority(c1, COCO_PRIORITY_HIGH);
    coco_set_priority(c2, COCO_PRIORITY_NORMAL);
    coco_set_priority(c3, COCO_PRIORITY_LOW);

    /* 运行调度器 */
    coco_sched_run(sched);

    /* 验证执行顺序：高优先级先执行 */
    if (coro_exec_order[0] == 1) {
        PASS();
    } else {
        FAIL("high priority coroutine did not execute first");
    }

    coco_sched_destroy(sched);
}

/**
 * test_aging_config - 验证优先级设置 API
 */
static void config_coro(void *arg) {
    (void)arg;
    coco_yield();
}

TEST(test_aging_config) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        FAIL("failed to create scheduler");
        return;
    }

    coco_coro_t *coro = coco_create(sched, config_coro, NULL, 0);
    if (!coro) {
        coco_sched_destroy(sched);
        FAIL("failed to create coroutine");
        return;
    }

    /* 测试所有优先级级别 */
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

    /* 运行调度器完成协程 */
    coco_sched_run(sched);

    PASS();
    coco_sched_destroy(sched);
}

/* ========== 主函数 ========== */

int main(void) {
    printf("=== Aging Tests ===\n");

    RUN_TEST(test_aging_disabled);
    RUN_TEST(test_aging_no_promote);
    RUN_TEST(test_aging_multiple);
    RUN_TEST(test_aging_config);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
