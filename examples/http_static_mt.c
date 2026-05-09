/**
 * http_static_mt.c - HTTP 静态文件服务器示例（多线程版本）
 *
 * 使用 coco 全局调度器实现多线程协程调度。
 * 工作线程自动分发协程，充分利用多核 CPU。
 *
 * 注意：多线程模式下使用阻塞 I/O，因为每个连接在独立协程中执行。
 */

#define _GNU_SOURCE
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
#include <sys/sendfile.h>
#include <poll.h>

#define DEFAULT_PORT 8080
#define DEFAULT_DIR "."
#define BUFFER_SIZE 8192
#define CHUNK_SIZE 8192
#define MAX_PATH 1024

static volatile sig_atomic_t g_running = 1;
static int g_listen_fd = -1;

/* HTTP 请求结构 */
typedef struct {
    char method[16];
    char path[MAX_PATH];
    char version[16];
    bool keep_alive;
} http_request_t;

/* HTTP 响应结构 */
typedef struct {
    int status_code;
    const char *status_text;
    const char *content_type;
    size_t content_length;
} http_response_t;

/* URL 解码 */
static int url_decode(const char *src, char *dst, size_t dst_size) {
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return 0;
}

/* 解析 HTTP 请求 */
static int parse_http_request(const char *buffer, size_t len, http_request_t *req) {
    (void)len;
    memset(req, 0, sizeof(*req));
    req->keep_alive = false;

    int n = sscanf(buffer, "%15s %1023s %15s", req->method, req->path, req->version);
    if (n != 3) return -1;

    if (strncmp(req->version, "HTTP/1.", 7) != 0) return -1;

    const char *conn = strcasestr(buffer, "\r\nConnection:");
    if (conn) {
        conn += 13;
        while (*conn == ' ') conn++;
        if (strncasecmp(conn, "keep-alive", 10) == 0) {
            req->keep_alive = true;
        }
    }

    if (strcmp(req->version, "HTTP/1.1") == 0 && !strcasestr(buffer, "\r\nConnection: close")) {
        req->keep_alive = true;
    }

    return 0;
}

/* 构建响应头 */
static int build_response_header(char *buffer, size_t size, const http_response_t *resp) {
    return snprintf(buffer, size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        resp->status_code, resp->status_text,
        resp->content_type ? resp->content_type : "application/octet-stream",
        resp->content_length);
}

/* 使用协程安全的 I/O 操作
 * coco_read/coco_write 在 fd 未就绪时自动 yield，
 * 等待 netpoller 线程唤醒。
 */
static ssize_t mt_read(int fd, void *buf, size_t count) {
    return coco_read(fd, buf, count);
}

static ssize_t mt_write(int fd, const void *buf, size_t count) {
    return coco_write(fd, buf, count);
}

/* 发送错误响应 */
static int send_error_response(int fd, int code, const char *text) {
    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1></body></html>",
        code, text, code, text);

    http_response_t resp = {
        .status_code = code,
        .status_text = text,
        .content_type = "text/html; charset=utf-8",
        .content_length = body_len
    };

    char header[512];
    int header_len = build_response_header(header, sizeof(header), &resp);

    mt_write(fd, header, header_len);
    mt_write(fd, body, body_len);

    return 0;
}

/* 验证路径 */
static int validate_path(const char *root_dir, const char *req_path,
                         char *safe_path, size_t safe_path_size) {
    char resolved_root[PATH_MAX];
    char full_path[PATH_MAX];
    char resolved_path[PATH_MAX];

    if (!realpath(root_dir, resolved_root)) {
        return 500;
    }

    const char *p = req_path;
    while (*p == '/') p++;

    if (*p == '\0') {
        size_t len = strlen(resolved_root);
        if (len >= safe_path_size) {
            return 500;
        }
        memcpy(safe_path, resolved_root, len + 1);
        return 0;
    }

    size_t root_len = strlen(resolved_root);
    size_t req_len = strlen(p);
    /* 确保 full_path 有足够空间 */
    if (root_len >= sizeof(full_path) - 2 || req_len >= sizeof(full_path) - root_len - 1) {
        return 500;
    }
    memcpy(full_path, resolved_root, root_len);
    full_path[root_len] = '/';
    memcpy(full_path + root_len + 1, p, req_len + 1);

    if (!realpath(full_path, resolved_path)) {
        return 404;
    }

    size_t res_len = strlen(resolved_root);
    if (strncmp(resolved_path, resolved_root, res_len) != 0) {
        return 403;
    }

    if (resolved_path[res_len] != '\0' && resolved_path[res_len] != '/') {
        return 403;
    }

    size_t path_len = strlen(resolved_path);
    if (path_len >= safe_path_size) {
        return 500;
    }
    memcpy(safe_path, resolved_path, path_len + 1);

    return 0;
}

/* Content-Type 映射 */
static const char *get_content_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0)
        return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(ext, ".json") == 0)
        return "application/json";
    if (strcasecmp(ext, ".txt") == 0)
        return "text/plain; charset=utf-8";
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".gif") == 0)
        return "image/gif";
    if (strcasecmp(ext, ".svg") == 0)
        return "image/svg+xml";
    if (strcasecmp(ext, ".ico") == 0)
        return "image/x-icon";
    if (strcasecmp(ext, ".pdf") == 0)
        return "application/pdf";
    if (strcasecmp(ext, ".zip") == 0)
        return "application/zip";

    return "application/octet-stream";
}

/* 发送文件内容 */
static int send_file_content(int fd, const char *file_path, size_t file_size, bool head_only) {
    if (head_only) return 0;

    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) return -1;

    size_t sent = 0;
    const size_t chunk_size = 64 * 1024;

    while (sent < file_size && g_running) {
        size_t to_send = (file_size - sent > chunk_size) ? chunk_size : (file_size - sent);
        ssize_t n = sendfile(fd, file_fd, NULL, to_send);
        if (n <= 0) {
            close(file_fd);
            return -1;
        }
        sent += n;
    }

    close(file_fd);
    return g_running ? 0 : -1;
}

/* 发送文件响应 */
static int send_file_response(int fd, const char *file_path, bool head_only) {
    struct stat st;
    if (stat(file_path, &st) < 0 || !S_ISREG(st.st_mode)) {
        return -1;
    }

    http_response_t resp = {
        .status_code = 200,
        .status_text = "OK",
        .content_type = get_content_type(file_path),
        .content_length = st.st_size
    };

    char header[512];
    int header_len = build_response_header(header, sizeof(header), &resp);

    mt_write(fd, header, header_len);

    return send_file_content(fd, file_path, st.st_size, head_only);
}

static const char *INDEX_FILES[] = {"index.html", "index.htm", NULL};

static int find_index_file(const char *dir_path, char *index_path, size_t size) {
    size_t dir_len = strlen(dir_path);
    for (int i = 0; INDEX_FILES[i]; i++) {
        size_t idx_len = strlen(INDEX_FILES[i]);
        if (dir_len + 1 + idx_len >= size) {
            continue;
        }
        snprintf(index_path, size, "%s/%s", dir_path, INDEX_FILES[i]);
        if (access(index_path, R_OK) == 0) {
            return 0;
        }
    }
    return -1;
}

/* 生成目录列表 */
static void send_directory_listing(int fd, const char *dir_path, const char *url_path, bool head_only) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        send_error_response(fd, 500, "Internal Server Error");
        return;
    }

    char *body = malloc(BUFFER_SIZE * 4);
    if (!body) {
        closedir(dir);
        send_error_response(fd, 500, "Internal Server Error");
        return;
    }

    int pos = snprintf(body, BUFFER_SIZE * 4,
        "<!DOCTYPE html>\n"
        "<html><head><title>Index of %s</title></head>\n"
        "<body><h1>Index of %s</h1><hr><pre>\n",
        url_path, url_path);

    if (strcmp(url_path, "/") != 0) {
        pos += snprintf(body + pos, BUFFER_SIZE * 4 - pos,
            "<a href=\"../\">../</a>\n");
    }

    struct dirent *entry;
    size_t dir_path_len = strlen(dir_path);
    while ((entry = readdir(dir)) != NULL && pos < BUFFER_SIZE * 4 - 256) {
        if (entry->d_name[0] == '.') continue;

        size_t name_len = strlen(entry->d_name);
        char full_path[PATH_MAX];
        if (dir_path_len + 1 + name_len < sizeof(full_path)) {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        } else {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) < 0) continue;

        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));

        if (S_ISDIR(st.st_mode)) {
            pos += snprintf(body + pos, BUFFER_SIZE * 4 - pos,
                "<a href=\"%s/\">%-40s</a>  -         -\n",
                entry->d_name, entry->d_name);
        } else {
            pos += snprintf(body + pos, BUFFER_SIZE * 4 - pos,
                "<a href=\"%s\">%-40s</a>  %10ld  %s\n",
                entry->d_name, entry->d_name, (long)st.st_size, time_str);
        }
    }

    closedir(dir);

    pos += snprintf(body + pos, BUFFER_SIZE * 4 - pos,
        "</pre><hr></body></html>\n");

    http_response_t resp = {
        .status_code = 200,
        .status_text = "OK",
        .content_type = "text/html; charset=utf-8",
        .content_length = pos
    };

    char header[512];
    int header_len = build_response_header(header, sizeof(header), &resp);

    mt_write(fd, header, header_len);
    if (!head_only) {
        mt_write(fd, body, pos);
    }

    free(body);
}

/* 处理请求路径 */
static void serve_path(int fd, const char *root_dir, const char *req_path, bool head_only) {
    char safe_path[PATH_MAX];
    int valid = validate_path(root_dir, req_path, safe_path, sizeof(safe_path));

    if (valid == 403) {
        send_error_response(fd, 403, "Forbidden");
        return;
    }
    if (valid == 500) {
        send_error_response(fd, 500, "Internal Server Error");
        return;
    }

    struct stat st;
    if (stat(safe_path, &st) < 0) {
        send_error_response(fd, 404, "Not Found");
        return;
    }

    if (S_ISREG(st.st_mode)) {
        if (send_file_response(fd, safe_path, head_only) < 0) {
            send_error_response(fd, 500, "Internal Server Error");
        }
    } else if (S_ISDIR(st.st_mode)) {
        char index_path[PATH_MAX];
        if (find_index_file(safe_path, index_path, sizeof(index_path)) == 0) {
            if (send_file_response(fd, index_path, head_only) < 0) {
                send_error_response(fd, 500, "Internal Server Error");
            }
        } else {
            send_directory_listing(fd, safe_path, req_path, head_only);
        }
    } else {
        send_error_response(fd, 403, "Forbidden");
    }
}

/* 客户端连接参数 */
struct client_arg {
    int fd;
    char root_dir[PATH_MAX];
};

/* 处理客户端连接 */
static void handle_client(void *arg) {
    struct client_arg *ca = arg;
    int client_fd = ca->fd;
    const char *root_dir = ca->root_dir;
    char buffer[BUFFER_SIZE];

    while (g_running) {
        int n = mt_read(client_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) break;

        buffer[n] = '\0';

        http_request_t req;
        if (parse_http_request(buffer, n, &req) < 0) {
            send_error_response(client_fd, 400, "Bad Request");
            break;
        }

        if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
            send_error_response(client_fd, 405, "Method Not Allowed");
            if (!req.keep_alive) break;
            continue;
        }

        char decoded_path[MAX_PATH];
        if (url_decode(req.path, decoded_path, sizeof(decoded_path)) < 0) {
            send_error_response(client_fd, 400, "Bad Request");
            break;
        }

        serve_path(client_fd, root_dir, decoded_path,
                   strcmp(req.method, "HEAD") == 0);

        if (!req.keep_alive) break;
    }

    close(client_fd);
    free(ca);
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

static void print_usage(const char *prog) {
    printf("Usage: %s [-d <dir>] [-p <port>] [-t <threads>] [-h]\n", prog);
    printf("  -d <dir>     Root directory for static files (default: .)\n");
    printf("  -p <port>    Port to listen on (default: 8080)\n");
    printf("  -t <threads> Number of worker threads (default: auto-detect)\n");
    printf("  -h           Show this help\n");
    printf("\nExample: %s -d /var/www/html -p 3000 -t 4\n", prog);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    const char *root_dir = DEFAULT_DIR;
    int num_threads = 0; /* 0 = auto-detect */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            root_dir = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
            if (num_threads < 0) {
                fprintf(stderr, "Invalid thread count: %s\n", argv[i]);
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

    printf("=== HTTP Static Server (coco multi-threaded) ===\n\n");
    printf("Root directory: %s\n", resolved_root);
    printf("Port: %d\n", port);
    printf("Worker threads: %s\n", num_threads > 0 ? argv[3] : "auto-detect");
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
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

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

    /* 更大的 backlog 以支持高并发 */
    if (listen(listen_fd, 4096) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    g_listen_fd = listen_fd;
    printf("Listening on port %d...\n", port);

    /* 启动全局调度器（多线程） */
    if (coco_global_sched_start(num_threads) < 0) {
        fprintf(stderr, "Failed to start global scheduler\n");
        close(listen_fd);
        return 1;
    }

    /* 接受连接并分发到工作线程 */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (!g_running) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 使用 poll 等待新连接 */
                struct pollfd pfd = { .fd = listen_fd, .events = POLLIN };
                int ret = poll(&pfd, 1, 100);
                if (ret < 0 && errno != EINTR) {
                    perror("poll");
                }
                continue;
            }
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        struct client_arg *ca = malloc(sizeof(*ca));
        if (ca) {
            ca->fd = client_fd;
            size_t root_len = strlen(resolved_root);
            if (root_len >= PATH_MAX) {
                root_len = PATH_MAX - 1;
            }
            memcpy(ca->root_dir, resolved_root, root_len);
            ca->root_dir[root_len] = '\0';
            /* coco_go 自动分发到工作线程，使用动态栈（初始 32KB，按需增长） */
            coco_go_opts_t opts = {
                .stack_size = 32 * 1024,
                .context = NULL,
                .priority = -1,
                .p_id = -1
            };
            coco_go_with_opts(handle_client, ca, &opts);
        } else {
            close(client_fd);
        }
    }

    /* 等待所有协程完成 */
    coco_global_sched_wait();
    coco_global_sched_stop();

    close(listen_fd);
    printf("Server stopped\n");
    return 0;
}
