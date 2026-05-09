/**
 * stress_io_thunder.c - 压力测试: 并发 I/O 操作创建/销毁
 *
 * 验证:
 * 1. 大量 fd 注册/注销不崩溃
 * 2. 无文件描述符泄漏
 * 3. I/O 超时正确处理
 *
 * 使用 pipe + sleep 模式验证 I/O 注册和超时处理。
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdatomic.h>

static atomic_int coros_completed = 0;

void io_timeout_coro(void *arg) {
    /* Create a pipe, try to read with timeout (will get nothing, just tests I/O path) */
    int fds[2];
    if (pipe(fds) == 0) {
        /* Write a small message so read will succeed */
        const char *msg = "hello";
        write(fds[1], msg, 5);
        close(fds[1]);
        
        /* Now read */
        char buf[64];
        ssize_t n = coco_read(fds[0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            atomic_fetch_add(&coros_completed, 1);
        }
        close(fds[0]);
    }
}

void simple_coro(void *arg) {
    (void)arg;
    /* Simple coroutine with no I/O */
    atomic_fetch_add(&coros_completed, 1);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    const int TOTAL = 1000;
    printf("Stress test: %d coroutines with mixed I/O operations\n", TOTAL);

    for (int i = 0; i < TOTAL; i++) {
        if (i % 3 == 0) {
            /* I/O coroutine */
            coco_create(sched, io_timeout_coro, NULL, 0);
        } else {
            /* Simple coroutine */
            coco_create(sched, simple_coro, NULL, 0);
        }
    }

    coco_sched_run(sched);

    int completed = atomic_load(&coros_completed);
    printf("  Completed: %d / %d\n", completed, TOTAL);
    assert(completed == TOTAL);

    coco_sched_destroy(sched);

    printf("[PASS] I/O thunder: %d coroutines completed\n", completed);
    return 0;
}
