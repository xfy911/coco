/**
 * test_sched_hooks.c - 调度器钩子系统测试 (US-015)
 *
 * 验收标准:
 * - src/sched/sched_hooks.h 定义 coco_hook_type_t 枚举（含 ON_DESTROY）
 * - src/sched/sched_hooks.h 定义 coco_hook_fn 返回 int 支持中断
 * - src/sched/sched_hooks.c 实现 coco_hook_register/unregister/invoke
 * - cmake 编译通过，ctest 测试通过
 */

#include "../../src/sched/sched_hooks.h"
#include "../../include/coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int test_pass_count = 0;
static int test_fail_count = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        test_pass_count++; \
        printf("  ✓ %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("  ✗ %s\n", msg); \
    } \
} while (0)

/* 测试计数器 */
static int hook_call_count = 0;
static int hook_return_value = 0;

/* 测试钩子函数 */
static int test_hook(coco_coro_t *coro, void *data) {
    hook_call_count++;
    return hook_return_value;
}

/* 测试钩子函数2 */
static int test_hook2(coco_coro_t *coro, void *data) {
    hook_call_count += 10;
    return 0;
}

/* 测试 1: 枚举定义 */
static void test_enum_definition(void) {
    printf("\n[TEST 1] 枚举定义\n");

    TEST_ASSERT(COCO_HOOK_ON_CREATE == 0, "COCO_HOOK_ON_CREATE 定义正确");
    TEST_ASSERT(COCO_HOOK_ON_SCHEDULE == 1, "COCO_HOOK_ON_SCHEDULE 定义正确");
    TEST_ASSERT(COCO_HOOK_ON_EXIT == 2, "COCO_HOOK_ON_EXIT 定义正确");
    TEST_ASSERT(COCO_HOOK_ON_DESTROY == 3, "COCO_HOOK_ON_DESTROY 定义正确");
    TEST_ASSERT(COCO_HOOK_ON_STEAL == 4, "COCO_HOOK_ON_STEAL 定义正确");
    TEST_ASSERT(COCO_HOOK_ON_BLOCK == 5, "COCO_HOOK_ON_BLOCK 定义正确");
    TEST_ASSERT(COCO_HOOK_ON_WAKE == 6, "COCO_HOOK_ON_WAKE 定义正确");
    TEST_ASSERT(COCO_HOOK_COUNT == 7, "COCO_HOOK_COUNT 定义正确");
}

/* 测试 2: 注册和注销 */
static void test_register_unregister(void) {
    printf("\n[TEST 2] 注册和注销\n");

    /* 清除所有钩子 */
    coco_hook_clear_all();

    int ret = coco_hook_register(COCO_HOOK_ON_CREATE, test_hook, NULL);
    TEST_ASSERT(ret == 0, "注册钩子成功");
    TEST_ASSERT(coco_hook_enabled(), "钩子已启用");

    ret = coco_hook_unregister(COCO_HOOK_ON_CREATE, test_hook);
    TEST_ASSERT(ret == 0, "注销钩子成功");
    TEST_ASSERT(!coco_hook_enabled(), "钩子已禁用");

    /* 注销不存在的钩子 */
    ret = coco_hook_unregister(COCO_HOOK_ON_CREATE, test_hook);
    TEST_ASSERT(ret == -1, "注销不存在的钩子返回 -1");
}

/* 测试 3: 调用钩子 */
static void test_invoke_hook(void) {
    printf("\n[TEST 3] 调用钩子\n");

    coco_hook_clear_all();
    hook_call_count = 0;
    hook_return_value = 0;

    coco_hook_register(COCO_HOOK_ON_CREATE, test_hook, NULL);
    int ret = coco_hook_invoke(COCO_HOOK_ON_CREATE, NULL);
    TEST_ASSERT(ret == 0, "调用钩子返回 0");
    TEST_ASSERT(hook_call_count == 1, "钩子被调用一次");

    coco_hook_clear_all();
}

/* 测试 4: 多个钩子 */
static void test_multiple_hooks(void) {
    printf("\n[TEST 4] 多个钩子\n");

    coco_hook_clear_all();
    hook_call_count = 0;

    coco_hook_register(COCO_HOOK_ON_CREATE, test_hook, NULL);
    coco_hook_register(COCO_HOOK_ON_CREATE, test_hook2, NULL);

    coco_hook_invoke(COCO_HOOK_ON_CREATE, NULL);
    TEST_ASSERT(hook_call_count == 11, "两个钩子都被调用");

    coco_hook_clear_all();
}

/* 测试 5: 钩子中断 */
static void test_hook_interrupt(void) {
    printf("\n[TEST 5] 钩子中断\n");

    coco_hook_clear_all();
    hook_call_count = 0;
    hook_return_value = 1;  /* 第一个钩子返回非0 */

    /* 注册顺序：后注册的先调用 */
    coco_hook_register(COCO_HOOK_ON_CREATE, test_hook2, NULL);  /* 后调用 */
    coco_hook_register(COCO_HOOK_ON_CREATE, test_hook, NULL);   /* 先调用，返回1中断 */

    int ret = coco_hook_invoke(COCO_HOOK_ON_CREATE, NULL);
    TEST_ASSERT(ret == 1, "钩子返回非0");
    TEST_ASSERT(hook_call_count == 1, "只有第一个钩子被调用");

    coco_hook_clear_all();
    hook_return_value = 0;
}

/* 测试 6: 无效参数 */
static void test_invalid_args(void) {
    printf("\n[TEST 6] 无效参数\n");

    int ret = coco_hook_register(COCO_HOOK_COUNT, test_hook, NULL);
    TEST_ASSERT(ret == -1, "无效类型返回 -1");

    ret = coco_hook_register(COCO_HOOK_ON_CREATE, NULL, NULL);
    TEST_ASSERT(ret == -1, "NULL 函数返回 -1");

    ret = coco_hook_unregister(COCO_HOOK_COUNT, test_hook);
    TEST_ASSERT(ret == -1, "注销无效类型返回 -1");
}

/* 测试 7: 清除所有钩子 */
static void test_clear_all(void) {
    printf("\n[TEST 7] 清除所有钩子\n");

    coco_hook_register(COCO_HOOK_ON_CREATE, test_hook, NULL);
    coco_hook_register(COCO_HOOK_ON_EXIT, test_hook, NULL);
    coco_hook_register(COCO_HOOK_ON_DESTROY, test_hook, NULL);

    TEST_ASSERT(coco_hook_enabled(), "有钩子注册");

    coco_hook_clear_all();
    TEST_ASSERT(!coco_hook_enabled(), "清除后无钩子");
}

int main(void) {
    printf("=== 调度器钩子系统测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. coco_hook_type_t 枚举定义（含 ON_DESTROY）\n");
    printf("  2. coco_hook_fn 返回 int 支持中断\n");
    printf("  3. coco_hook_register/unregister/invoke 实现\n");

    test_enum_definition();
    test_register_unregister();
    test_invoke_hook();
    test_multiple_hooks();
    test_hook_interrupt();
    test_invalid_args();
    test_clear_all();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %d\n", test_pass_count);
    printf("失败: %d\n", test_fail_count);

    if (test_fail_count == 0) {
        printf("\n✓ 所有测试通过！US-015 验收标准达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}
