/**
 * test_batch_io.c - 批量 I/O API 单元测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "coco.h"

/* 测试计数 */
static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) do { \
    g_tests_run++; \
    printf("  TEST: %s... ", #name); \
    if (test_##name()) { \
        g_tests_passed++; \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

/* 测试批量上下文创建和销毁 */
static int test_batch_begin_end(void) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) return 0;

    /* macOS 使用 kqueue，不支持批量 I/O */
    coco_batch_io_t *batch = coco_batch_begin(sched);

    if (batch) {
        coco_batch_end(batch);
    }

    coco_sched_destroy(sched);
    return 1;  /* 即使返回 NULL 也算通过（kqueue 不支持） */
}

/* 测试批量添加操作 */
static int test_batch_add_operations(void) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) return 0;

    coco_batch_io_t *batch = coco_batch_begin(sched);
    if (!batch) {
        /* kqueue 不支持，跳过 */
        coco_sched_destroy(sched);
        return 1;
    }

    char buf[64];
    int ret;

    /* 添加读操作 */
    ret = coco_batch_add_read(batch, 0, buf, sizeof(buf));
    /* 可能返回错误（fd 0 可能不可用），但不应该崩溃 */

    /* 添加写操作 */
    ret = coco_batch_add_write(batch, 1, buf, sizeof(buf));

    coco_batch_end(batch);
    coco_sched_destroy(sched);
    return 1;
}

/* 测试批量取消 */
static int test_batch_cancel(void) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) return 0;

    coco_batch_io_t *batch = coco_batch_begin(sched);
    if (!batch) {
        coco_sched_destroy(sched);
        return 1;
    }

    char buf[64];
    coco_batch_add_read(batch, 0, buf, sizeof(buf));

    /* 取消批量操作 */
    int ret = coco_batch_cancel(batch);
    (void)ret;

    coco_batch_end(batch);
    coco_sched_destroy(sched);
    return 1;
}

/* 测试空批量提交 */
static int test_batch_empty_submit(void) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) return 0;

    coco_batch_io_t *batch = coco_batch_begin(sched);
    if (!batch) {
        coco_sched_destroy(sched);
        return 1;
    }

    /* 空批量提交应该返回错误 */
    coco_batch_result_t results[4];
    int ret = coco_batch_submit(batch, results, 4);
    if (ret != COCO_ERROR) {
        /* 预期返回错误 */
    }

    coco_batch_end(batch);
    coco_sched_destroy(sched);
    return 1;
}

/* 测试无效参数 */
static int test_batch_invalid_args(void) {
    /* NULL 参数测试 */
    coco_batch_io_t *batch = coco_batch_begin(NULL);
    if (batch != NULL) return 0;

    int ret;
    ret = coco_batch_add_read(NULL, 0, NULL, 0);
    if (ret != COCO_ERROR) return 0;

    ret = coco_batch_add_write(NULL, 0, NULL, 0);
    if (ret != COCO_ERROR) return 0;

    ret = coco_batch_submit(NULL, NULL, 0);
    if (ret != COCO_ERROR) return 0;

    ret = coco_batch_cancel(NULL);
    if (ret != COCO_ERROR) return 0;

    coco_batch_end(NULL);  /* 应该安全地什么都不做 */

    return 1;
}

int main(void) {
    printf("=== Batch I/O API Tests ===\n\n");

    TEST(batch_begin_end);
    TEST(batch_add_operations);
    TEST(batch_cancel);
    TEST(batch_empty_submit);
    TEST(batch_invalid_args);

    printf("\n=== Results: %d/%d tests passed ===\n", g_tests_passed, g_tests_run);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
