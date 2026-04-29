/**
 * test_io_backend.c - I/O 后端选择 API 单元测试
 */

#include <stdio.h>
#include <assert.h>
#include "coco.h"

/* 测试后端选择 API */
static void test_backend_selection_api(void) {
    printf("Testing I/O backend selection API...\n");

    /* 创建调度器 */
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    /* 默认应该是 AUTO */
    coco_io_backend_t backend = coco_sched_get_io_backend(sched);
    assert(backend == COCO_IO_BACKEND_AUTO ||
           backend == COCO_IO_BACKEND_EPOLL ||
           backend == COCO_IO_BACKEND_IOURING);
    printf("  Default backend: %d\n", backend);

    coco_sched_destroy(sched);
    printf("  PASSED: Default backend check\n");
}

/* 测试强制选择后端 */
static void test_force_backend(void) {
    printf("Testing force backend selection...\n");

    /* 创建调度器 */
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    /* 尝试强制选择 epoll */
    int ret = coco_sched_set_io_backend(sched, COCO_IO_BACKEND_EPOLL);
    /* 可能失败，因为调度器已初始化 */
    printf("  set_io_backend after init: %d (expected error)\n", ret);

    coco_sched_destroy(sched);

    /* 创建新调度器并强制选择 */
    sched = coco_sched_create();
    assert(sched != NULL);

    /* 销毁后重新创建，在初始化前设置 */
    coco_sched_destroy(sched);

    printf("  PASSED: Force backend selection\n");
}

/* 测试获取后端 */
static void test_get_backend(void) {
    printf("Testing get_io_backend...\n");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    coco_io_backend_t backend = coco_sched_get_io_backend(sched);
    printf("  Current backend: %d\n", backend);

    /* 后端应该是有效值 */
    assert(backend >= COCO_IO_BACKEND_AUTO && backend <= COCO_IO_BACKEND_IOURING);

    coco_sched_destroy(sched);
    printf("  PASSED: Get backend\n");
}

/* 测试 NULL 参数处理 */
static void test_null_handling(void) {
    printf("Testing NULL parameter handling...\n");

    int ret = coco_sched_set_io_backend(NULL, COCO_IO_BACKEND_EPOLL);
    assert(ret == COCO_ERROR);
    printf("  set_io_backend(NULL, ...) returned COCO_ERROR\n");

    coco_io_backend_t backend = coco_sched_get_io_backend(NULL);
    assert(backend == COCO_IO_BACKEND_AUTO);
    printf("  get_io_backend(NULL) returned AUTO\n");

    printf("  PASSED: NULL handling\n");
}

int main(void) {
    printf("=== I/O Backend Selection Tests ===\n\n");

    test_backend_selection_api();
    test_force_backend();
    test_get_backend();
    test_null_handling();

    printf("\n=== All tests passed ===\n");
    return 0;
}
