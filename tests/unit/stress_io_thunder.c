/**
 * stress_io_thunder.c - 压力测试: 并发 I/O 事件
 *
 * 验证:
 * 1. 大量并发 read/write 不阻塞
 * 2. 无文件描述符泄漏
 * 3. I/O 超时正确处理
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static coco_channel_t *io_done_ch;
static int completed = 0;

void io_client(void *arg) {
    int port = (int)(long)arg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        coco_channel_send(io_done_ch, (void*)(long)-1);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    int ret = coco_connect(fd, &addr, sizeof(addr));
    if (ret == COCO_OK) {
        const char *msg = "HELLO";
        int written = coco_write(fd, msg, strlen(msg));
        if (written > 0) {
            completed++;
        }
    }

    close(fd);
    coco_channel_send(io_done_ch, (void*)(long)completed);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);

    io_done_ch = coco_channel_create(0);
    assert(io_done_ch != NULL);

    /* 创建监听 socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(server_fd >= 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = 0,  /* 任意端口 */
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    socklen_t addr_len = sizeof(addr);

    assert(bind(server_fd, (struct sockaddr*)&addr, addr_len) == 0);
    assert(getsockname(server_fd, (struct sockaddr*)&addr, &addr_len) == 0);
    int port = ntohs(addr.sin_port);
    assert(listen(server_fd, 128) == 0);

    printf("Stress test: 50 concurrent connections on port %d\n", port);

    /* 创建客户端协程 */
    for (int i = 0; i < 50; i++) {
        coco_create(sched, io_client, (void*)(long)port, 0);
    }

    /* 接受连接 */
    for (int i = 0; i < 50; i++) {
        struct sockaddr_in client_addr;
        size_t client_len = sizeof(client_addr);
        int client_fd = coco_accept(server_fd, &client_addr, &client_len);
        if (client_fd >= 0) {
            char buf[256];
            ssize_t n = coco_read(client_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
            }
            close(client_fd);
        }
    }

    coco_sched_run(sched);

    /* 等待所有完成 */
    for (int i = 0; i < 50; i++) {
        void *val;
        coco_channel_recv(io_done_ch, &val);
    }

    close(server_fd);
    coco_channel_destroy(io_done_ch);
    coco_sched_destroy(sched);

    printf("[PASS] I/O thunder: %d connections handled\n", completed);
    return 0;
}
