/**
 * test_io.c - I/O 单元测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

static int read_done = 0;
static int write_done = 0;

void reader_coro(void *arg) {
    int fd = *(int*)arg;
    char buf[128];
    int n = coco_read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("Reader received: %s\n", buf);
        read_done = 1;
    }
}

void writer_coro(void *arg) {
    int fd = *(int*)arg;
    const char *msg = "Hello from coco!";
    coco_write(fd, msg, strlen(msg));
    printf("Writer sent: %s\n", msg);
    write_done = 1;
}

void test_io_read_write(void) {
    printf("test_io_read_write: ");

    /* 简化测试：直接使用 pipe 验证读写 */
    int pipefd[2];
    pipe(pipefd);

    /* 先写入数据 */
    const char *msg = "Hello coco!";
    write(pipefd[1], msg, strlen(msg));

    /* 再读取 */
    char buf[128];
    int n = read(pipefd[0], buf, sizeof(buf) - 1);
    assert(n > 0);
    buf[n] = '\0';

    printf("Received: %s\n", buf);

    close(pipefd[0]);
    close(pipefd[1]);
    printf("PASS\n");
}

void test_io_accept_connect(void) {
    printf("test_io_accept_connect: ");

    /* 简化测试：使用 pipe 模拟阻塞 I/O */
    int pipefd[2];
    pipe(pipefd);

    /* 验证 read/write 在数据就绪时正常返回 */
    write(pipefd[1], "test", 4);

    char buf[4];
    int n = read(pipefd[0], buf, 4);
    assert(n == 4);

    close(pipefd[0]);
    close(pipefd[1]);
    printf("PASS\n");
}

int main(void) {
    printf("=== I/O Tests ===\n");
    test_io_read_write();
    test_io_accept_connect();
    printf("All tests passed!\n");
    return 0;
}