/**
 * http_static.c - HTTP 静态文件服务器示例
 *
 * 使用 coco 协程库实现类似 nginx 的静态文件托管。
 * 支持：
 *   - GET/HEAD 请求
 *   - 目录索引文件 (index.html/index.htm)
 *   - 目录列表 (autoindex)
 *   - 分块文件传输
 *   - 目录遍历防护
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#define DEFAULT_PORT 8080
#define DEFAULT_DIR "."
#define BUFFER_SIZE 8192
#define CHUNK_SIZE 8192
#define REQUEST_TIMEOUT_MS 30000
#define MAX_PATH 1024

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [-d <dir>] [-p <port>] [-h]\n", prog);
    printf("  -d <dir>   Root directory for static files (default: .)\n");
    printf("  -p <port>  Port to listen on (default: 8080)\n");
    printf("  -h         Show this help\n");
    printf("\nExample: %s -d /var/www/html -p 3000\n", prog);
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    const char *root_dir = DEFAULT_DIR;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            root_dir = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    char resolved_root[PATH_MAX];
    if (!realpath(root_dir, resolved_root)) {
        fprintf(stderr, "Invalid root directory: %s (%s)\n", root_dir, strerror(errno));
        return 1;
    }

    printf("=== HTTP Static Server (coco example) ===\n\n");
    printf("Root directory: %s\n", resolved_root);
    printf("Port: %d\n", port);
    printf("\nTest with: curl http://localhost:%d/\n\n", port);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    set_nonblock(listen_fd);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

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

    printf("Listening on port %d...\n", port);

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        close(listen_fd);
        return 1;
    }

    /* TODO: Add accept loop coroutine */

    coco_sched_run(sched);

    coco_sched_destroy(sched);
    close(listen_fd);
    printf("Server stopped\n");
    return 0;
}