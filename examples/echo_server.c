/**
 * echo_server.c - TCP echo 服务器示例
 *
 * 展示协程化 I/O：使用 coco_read/write/accept 实现高并发服务器。
 */

#include "coco.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#define PORT 8888
#define BUFFER_SIZE 1024

/* 传递给 accept_loop 的参数 */
struct accept_arg {
    coco_sched_t *sched;
    int fd;
};

/* 设置 fd 为非阻塞 */
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* 处理客户端连接 */
void handle_client(void *arg) {
    int client_fd = (int)(intptr_t)arg;
    char buffer[BUFFER_SIZE];

    printf("Client connected, fd=%d\n", client_fd);

    while (1) {
        int n = coco_read(client_fd, buffer, BUFFER_SIZE - 1);
        if (n <= 0) {
            printf("Client disconnected, fd=%d\n", client_fd);
            break;
        }

        buffer[n] = '\0';
        printf("Received: %s", buffer);

        /* Echo back */
        coco_write(client_fd, buffer, n);
    }

    close(client_fd);
}

/* 接受连接协程 */
void accept_loop(void *arg) {
    struct accept_arg *a = (struct accept_arg *)arg;
    coco_sched_t *sched = a->sched;
    int listen_fd = a->fd;

    printf("Accept loop started on port %d\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        size_t addrlen = sizeof(client_addr);

        int client_fd = coco_accept(listen_fd, &client_addr, &addrlen);
        if (client_fd < 0) {
            continue;
        }

        set_nonblock(client_fd);

        /* 为每个客户端创建新协程，fd 通过值传递避免生命周期风险 */
        coco_create(sched, handle_client, (void *)(intptr_t)client_fd, 0);
    }
}

int main(void) {
    printf("=== Echo Server Example ===\n\n");
    printf("Run: nc localhost 8888 to test\n\n");

    /* 创建监听 socket */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    set_nonblock(listen_fd);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("Listening on port %d...\n", PORT);

    /* 创建调度器和接受协程 */
    coco_sched_t *sched = coco_sched_create();
    struct accept_arg a = { .sched = sched, .fd = listen_fd };
    coco_create(sched, accept_loop, &a, 0);

    /* 运行服务器 */
    coco_sched_run(sched);

    coco_sched_destroy(sched);
    close(listen_fd);
    return 0;
}