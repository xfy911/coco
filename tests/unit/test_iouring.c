/**
 * test_iouring.c - io_uring 单元测试
 *
 * 测试 io_uring 后端功能（仅在 Linux 上运行）。
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef __linux__

/* 测试 io_uring 初始化和清理 */
static void test_iouring_init(void) {
    printf("test_iouring_init: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    /* 检查 I/O 后端类型 */
    assert(sched->poll_backend == COCO_POLL_IOURING ||
           sched->poll_backend == COCO_POLL_EPOLL);

    /* 如果是 io_uring，检查初始化正确 */
    if (sched->poll_backend == COCO_POLL_IOURING) {
        assert(sched->iouring != NULL);
        assert(sched->poll_fd >= 0);
    }

    coco_sched_destroy(sched);

    printf("PASS\n");
}

/* 测试 io_uring 网络 I/O */
static void test_iouring_network_io(void) {
    printf("test_iouring_network_io: ");

    /* 创建 socket pair */
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    /* 创建协程进行读写测试 */
    int read_done = 0;
    int write_done = 0;

    void reader(void *arg) {
        char buf[64];
        int n = coco_read(sv[0], buf, sizeof(buf));
        if (n > 0) {
            read_done = 1;
        }
    }

    void writer(void *arg) {
        const char *msg = "hello";
        int n = coco_write(sv[1], msg, strlen(msg));
        if (n > 0) {
            write_done = 1;
        }
    }

    coco_coro_t *r = coco_create(sched, reader, NULL, 0);
    coco_coro_t *w = coco_create(sched, writer, NULL, 0);
    (void)r; (void)w;

    coco_sched_run(sched);

    assert(read_done == 1);
    assert(write_done == 1);

    close(sv[0]);
    close(sv[1]);
    coco_sched_destroy(sched);

    printf("PASS\n");
}

/* 测试 io_uring 统计信息 */
static void test_iouring_stats(void) {
    printf("test_iouring_stats: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    if (sched->poll_backend == COCO_POLL_IOURING) {
        uint64_t submit_count, syscall_count;
        coco_iouring_get_stats(sched, &submit_count, &syscall_count);

        assert(submit_count == 0);
        assert(syscall_count == 0);
    }

    coco_sched_destroy(sched);

    printf("PASS\n");
}

/* 测试批量提交 */
static void test_iouring_batch_submit(void) {
    printf("test_iouring_batch_submit: ");

    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    if (sched->poll_backend == COCO_POLL_IOURING) {
        /* 批量提交应该返回 0（没有待提交请求） */
        int ret = coco_iouring_submit_batch(sched);
        assert(ret == 0);
    }

    coco_sched_destroy(sched);

    printf("PASS\n");
}

int main(void) {
    printf("=== io_uring Tests ===\n");

    test_iouring_init();
    test_iouring_network_io();
    test_iouring_stats();
    test_iouring_batch_submit();

    printf("\nAll io_uring tests passed!\n");
    return 0;
}

#else /* !__linux__ */

int main(void) {
    printf("=== io_uring Tests ===\n");
    printf("Skipped: not running on Linux\n");
    return 0;
}

#endif /* __linux__ */
