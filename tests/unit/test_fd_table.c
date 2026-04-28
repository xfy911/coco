/**
 * test_fd_table.c - FD 表动态扩容测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* 测试 FD 表创建和销毁 */
static void test_fd_table_create(void) {
    printf("test_fd_table_create: ");

    fd_table_t *ft = fd_table_create(0);
    assert(ft != NULL);
    assert(ft->capacity >= 1024);

    fd_table_destroy(ft);

    printf("PASS\n");
}

/* 测试自定义初始容量 */
static void test_fd_table_custom_capacity(void) {
    printf("test_fd_table_custom_capacity: ");

    fd_table_t *ft = fd_table_create(2048);
    assert(ft != NULL);
    assert(ft->capacity >= 2048);

    fd_table_destroy(ft);

    printf("PASS\n");
}

/* 测试 FD 表基本操作 */
static void test_fd_table_basic_ops(void) {
    printf("test_fd_table_basic_ops: ");

    fd_table_t *ft = fd_table_create(0);
    assert(ft != NULL);

    /* 创建模拟协程指针 */
    coco_coro_t mock_coro;
    memset(&mock_coro, 0, sizeof(mock_coro));

    /* 设置和获取 */
    assert(fd_table_set(ft, 100, &mock_coro) == COCO_OK);
    assert(fd_table_get(ft, 100) == &mock_coro);

    /* 清除 */
    fd_table_clear(ft, 100);
    assert(fd_table_get(ft, 100) == NULL);

    /* 边界检查 */
    assert(fd_table_get(ft, -1) == NULL);
    assert(fd_table_set(ft, -1, &mock_coro) == COCO_ERROR);

    fd_table_destroy(ft);

    printf("PASS\n");
}

/* 测试 FD 表动态扩容 */
static void test_fd_table_expand(void) {
    printf("test_fd_table_expand: ");

    fd_table_t *ft = fd_table_create(0);
    assert(ft != NULL);

    uint32_t initial_capacity = ft->capacity;

    coco_coro_t mock_coro;
    memset(&mock_coro, 0, sizeof(mock_coro));

    /* 设置超出初始容量的 FD */
    int large_fd = initial_capacity + 1000;
    assert(fd_table_set(ft, large_fd, &mock_coro) == COCO_OK);

    /* 验证扩容 */
    assert(ft->capacity > initial_capacity);
    assert(fd_table_get(ft, large_fd) == &mock_coro);

    /* 验证原有数据仍然有效 */
    assert(fd_table_set(ft, 50, &mock_coro) == COCO_OK);
    assert(fd_table_get(ft, 50) == &mock_coro);

    fd_table_destroy(ft);

    printf("PASS\n");
}

/* 测试 FD 表高 FD 值 */
static void test_fd_table_high_fd(void) {
    printf("test_fd_table_high_fd: ");

    fd_table_t *ft = fd_table_create(0);
    assert(ft != NULL);

    coco_coro_t mock_coro;
    memset(&mock_coro, 0, sizeof(mock_coro));

    /* 测试高 FD 值 */
    int high_fd = 10000;
    assert(fd_table_set(ft, high_fd, &mock_coro) == COCO_OK);
    assert(fd_table_get(ft, high_fd) == &mock_coro);

    fd_table_destroy(ft);

    printf("PASS\n");
}

/* 测试多个 FD 值 */
static void test_fd_table_multiple(void) {
    printf("test_fd_table_multiple: ");

    fd_table_t *ft = fd_table_create(0);
    assert(ft != NULL);

    coco_coro_t mock_coros[100];
    memset(mock_coros, 0, sizeof(mock_coros));

    /* 设置多个 FD */
    for (int i = 0; i < 100; i++) {
        assert(fd_table_set(ft, i * 10, &mock_coros[i]) == COCO_OK);
    }

    /* 验证所有值 */
    for (int i = 0; i < 100; i++) {
        assert(fd_table_get(ft, i * 10) == &mock_coros[i]);
    }

    /* 清除部分 */
    for (int i = 0; i < 50; i++) {
        fd_table_clear(ft, i * 10);
    }

    /* 验证清除结果 */
    for (int i = 0; i < 50; i++) {
        assert(fd_table_get(ft, i * 10) == NULL);
    }
    for (int i = 50; i < 100; i++) {
        assert(fd_table_get(ft, i * 10) == &mock_coros[i]);
    }

    fd_table_destroy(ft);

    printf("PASS\n");
}

int main(void) {
    printf("=== FD Table Tests ===\n");

    test_fd_table_create();
    test_fd_table_custom_capacity();
    test_fd_table_basic_ops();
    test_fd_table_expand();
    test_fd_table_high_fd();
    test_fd_table_multiple();

    printf("=== All FD Table Tests PASSED ===\n");
    return 0;
}
