/**
 * test_context_value.c - Context Value 传播测试 (US-014)
 *
 * 验收标准:
 * - src/core/context_api.h 定义 coco_context_value_t 结构（含 destructor）
 * - src/core/context_api.c 实现 coco_context_with_value() 带析构函数
 * - src/core/context_api.c 实现 coco_context_value() 查找 value
 * - src/core/context_api.c 在 coco_context_cancel() 中调用析构函数
 * - cmake 编译通过，ctest 测试通过
 */

#include "../../src/core/context_api.h"
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

/* 析构函数测试 */
static int destructor_call_count = 0;
static void *last_destructor_value = NULL;

static void test_destructor(void *value) {
    destructor_call_count++;
    last_destructor_value = value;
}

/* 测试 1: 结构体定义 */
static void test_struct_definition(void) {
    printf("\n[TEST 1] 结构体定义\n");

    coco_context_value_t vnode;
    vnode.key = "test";
    vnode.value = (void*)0x1234;
    vnode.destructor = test_destructor;
    vnode.next = NULL;

    TEST_ASSERT(vnode.key != NULL, "key 字段存在");
    TEST_ASSERT(vnode.value == (void*)0x1234, "value 字段存在");
    TEST_ASSERT(vnode.destructor == test_destructor, "destructor 字段存在");
}

/* 测试 2: coco_context_with_value 基本功能 */
static void test_with_value_basic(void) {
    printf("\n[TEST 2] coco_context_with_value 基本功能\n");

    coco_context_t *parent = coco_context_background();
    TEST_ASSERT(parent != NULL, "创建父 context");

    coco_context_t *ctx = coco_context_with_value(parent, "key1", (void*)123, NULL);
    TEST_ASSERT(ctx != NULL, "创建带 value 的 context");

    void *val = coco_context_value(ctx, "key1");
    TEST_ASSERT(val == (void*)123, "获取 value 正确");

    coco_context_unref(ctx);
    coco_context_unref(parent);
}

/* 测试 3: 嵌套 value */
static void test_nested_value(void) {
    printf("\n[TEST 3] 嵌套 value\n");

    coco_context_t *root = coco_context_background();
    coco_context_t *ctx1 = coco_context_with_value(root, "key1", (void*)111, NULL);
    coco_context_t *ctx2 = coco_context_with_value(ctx1, "key2", (void*)222, NULL);

    /* 子 context 可以访问父 context 的 value */
    void *val1 = coco_context_value(ctx2, "key1");
    TEST_ASSERT(val1 == (void*)111, "访问父 context 的 value");

    void *val2 = coco_context_value(ctx2, "key2");
    TEST_ASSERT(val2 == (void*)222, "访问自己的 value");

    /* 不存在的 key */
    void *val3 = coco_context_value(ctx2, "key3");
    TEST_ASSERT(val3 == NULL, "不存在的 key 返回 NULL");

    coco_context_unref(ctx2);
    coco_context_unref(ctx1);
    coco_context_unref(root);
}

/* 测试 4: 析构函数调用 */
static void test_destructor_called(void) {
    printf("\n[TEST 4] 析构函数调用\n");

    destructor_call_count = 0;
    last_destructor_value = NULL;

    coco_context_t *parent = coco_context_background();
    coco_context_t *ctx = coco_context_with_value(parent, "key1", (void*)999, test_destructor);
    TEST_ASSERT(ctx != NULL, "创建带析构函数的 context");

    /* 释放 context */
    coco_context_unref(ctx);
    coco_context_unref(parent);

    TEST_ASSERT(destructor_call_count == 1, "析构函数被调用一次");
    TEST_ASSERT(last_destructor_value == (void*)999, "析构函数参数正确");
}

/* 测试 5: NULL 参数处理 */
static void test_null_args(void) {
    printf("\n[TEST 5] NULL 参数处理\n");

    coco_context_t *parent = coco_context_background();

    coco_context_t *ctx = coco_context_with_value(parent, NULL, (void*)123, NULL);
    TEST_ASSERT(ctx == NULL, "NULL key 返回 NULL");

    void *val = coco_context_value(NULL, "key");
    TEST_ASSERT(val == NULL, "NULL context 返回 NULL");

    val = coco_context_value(parent, NULL);
    TEST_ASSERT(val == NULL, "NULL key 查找返回 NULL");

    coco_context_unref(parent);
}

/* 测试 6: 多个 value */
static void test_multiple_values(void) {
    printf("\n[TEST 6] 多个 value\n");

    coco_context_t *root = coco_context_background();
    coco_context_t *ctx1 = coco_context_with_value(root, "a", (void*)1, NULL);
    coco_context_t *ctx2 = coco_context_with_value(ctx1, "b", (void*)2, NULL);
    coco_context_t *ctx3 = coco_context_with_value(ctx2, "c", (void*)3, NULL);

    TEST_ASSERT(coco_context_value(ctx3, "a") == (void*)1, "访问 a");
    TEST_ASSERT(coco_context_value(ctx3, "b") == (void*)2, "访问 b");
    TEST_ASSERT(coco_context_value(ctx3, "c") == (void*)3, "访问 c");

    coco_context_unref(ctx3);
    coco_context_unref(ctx2);
    coco_context_unref(ctx1);
    coco_context_unref(root);
}

int main(void) {
    printf("=== Context Value 传播测试 ===\n");
    printf("验收标准验证:\n");
    printf("  1. coco_context_value_t 结构定义（含 destructor）\n");
    printf("  2. coco_context_with_value() 带析构函数\n");
    printf("  3. coco_context_value() 查找 value\n");
    printf("  4. 析构函数在 context 销毁时调用\n");

    test_struct_definition();
    test_with_value_basic();
    test_nested_value();
    test_destructor_called();
    test_null_args();
    test_multiple_values();

    printf("\n=== 测试结果 ===\n");
    printf("通过: %d\n", test_pass_count);
    printf("失败: %d\n", test_fail_count);

    if (test_fail_count == 0) {
        printf("\n✓ 所有测试通过！US-014 验收标准达成。\n");
        return 0;
    } else {
        printf("\n✗ 有测试失败，需要修复。\n");
        return 1;
    }
}
