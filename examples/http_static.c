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
#include <sys/sendfile.h>

#define DEFAULT_PORT 8080
#define DEFAULT_DIR "."
#define BUFFER_SIZE 8192
#define CHUNK_SIZE 8192
#define REQUEST_TIMEOUT_MS 30000
#define MAX_PATH 1024

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

static volatile sig_atomic_t g_running = 1;
static int g_listen_fd = -1;

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
    (void)len; /* 未使用，保留参数以匹配 API 签名 */
    memset(req, 0, sizeof(*req));
    req->keep_alive = false;

    /* 解析请求行: METHOD PATH VERSION */
    int n = sscanf(buffer, "%15s %1023s %15s", req->method, req->path, req->version);
    if (n != 3) return -1;

    /* 检查 HTTP 版本 */
    if (strncmp(req->version, "HTTP/1.", 7) != 0) return -1;

    /* 检查 keep-alive */
    const char *conn = strcasestr(buffer, "\r\nConnection:");
    if (conn) {
        conn += 13;
        while (*conn == ' ') conn++;
        if (strncasecmp(conn, "keep-alive", 10) == 0) {
            req->keep_alive = true;
        }
    }

    /* HTTP/1.1 默认 keep-alive */
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

    coco_write(fd, header, header_len);
    coco_write(fd, body, body_len);

    return 0;
}

/* 验证请求路径是否在 root_dir 内（防止目录遍历）
 * 返回: 0=有效, 403=目录遍历, 404=不存在, 500=服务器错误
 */
static int validate_path(const char *root_dir, const char *req_path,
                         char *safe_path, size_t safe_path_size) {
    char resolved_root[PATH_MAX];
    char full_path[PATH_MAX];
    char resolved_path[PATH_MAX];

    /* 规范化 root_dir */
    if (!realpath(root_dir, resolved_root)) {
        return 500;
    }

    /* 跳过开头的 '/' */
    const char *p = req_path;
    while (*p == '/') p++;

    /* 构建完整路径 */
    if (*p == '\0') {
        /* 根目录请求 */
        strncpy(safe_path, resolved_root, safe_path_size - 1);
        safe_path[safe_path_size - 1] = '\0';
        return 0;
    }

    snprintf(full_path, sizeof(full_path), "%s/%s", resolved_root, p);

    /* 规范化请求路径 */
    if (!realpath(full_path, resolved_path)) {
        /* 文件/目录不存在 */
        return 404;
    }

    /* 验证规范化路径是否在 root_dir 内 */
    size_t root_len = strlen(resolved_root);
    if (strncmp(resolved_path, resolved_root, root_len) != 0) {
        return 403; /* 目录遍历攻击 */
    }

    /* 检查路径分隔符：确保是 root_dir 或 root_dir 的子路径 */
    if (resolved_path[root_len] != '\0' && resolved_path[root_len] != '/') {
        return 403; /* 类似 /www2 绕过 /www */
    }

    /* 复制安全路径 */
    strncpy(safe_path, resolved_path, safe_path_size - 1);
    safe_path[safe_path_size - 1] = '\0';

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

/* 发送文件内容（使用 sendfile 零拷贝，支持中断） */
static int send_file_content(int fd, const char *file_path, size_t file_size, bool head_only) {
    if (head_only) return 0;

    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) return -1;

    /* 分块发送，每块后检查中断信号 */
    size_t sent = 0;
    const size_t chunk_size = 64 * 1024; /* 64KB per chunk */

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

    coco_write(fd, header, header_len);

    return send_file_content(fd, file_path, st.st_size, head_only);
}

/* 索引文件列表 */
static const char *INDEX_FILES[] = {"index.html", "index.htm", NULL};

/* 查找索引文件 */
static int find_index_file(const char *dir_path, char *index_path, size_t size) {
    for (int i = 0; INDEX_FILES[i]; i++) {
        snprintf(index_path, size, "%s/%s", dir_path, INDEX_FILES[i]);
        if (access(index_path, R_OK) == 0) {
            return 0;
        }
    }
    return -1;
}

/* 生成目录列表 HTML */
static void send_directory_listing(int fd, const char *dir_path, const char *url_path, bool head_only) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        send_error_response(fd, 500, "Internal Server Error");
        return;
    }

    /* 构建 HTML */
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

    /* 父目录链接 */
    if (strcmp(url_path, "/") != 0) {
        pos += snprintf(body + pos, BUFFER_SIZE * 4 - pos,
            "<a href=\"../\">../</a>\n");
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && pos < BUFFER_SIZE * 4 - 256) {
        if (entry->d_name[0] == '.') continue; /* 跳过隐藏文件 */

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

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

    coco_write(fd, header, header_len);
    if (!head_only) {
        coco_write(fd, body, pos);
    }

    free(body);
}

/* 客户端连接参数 */
struct client_arg {
    coco_sched_t *sched;
    int fd;
    char root_dir[PATH_MAX];
};

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
        /* 普通文件 */
        if (send_file_response(fd, safe_path, head_only) < 0) {
            send_error_response(fd, 500, "Internal Server Error");
        }
    } else if (S_ISDIR(st.st_mode)) {
        /* 目录 */
        char index_path[PATH_MAX];
        if (find_index_file(safe_path, index_path, sizeof(index_path)) == 0) {
            /* 找到索引文件 */
            if (send_file_response(fd, index_path, head_only) < 0) {
                send_error_response(fd, 500, "Internal Server Error");
            }
        } else {
            /* 生成目录列表 */
            send_directory_listing(fd, safe_path, req_path, head_only);
        }
    } else {
        send_error_response(fd, 403, "Forbidden");
    }
}

/* 处理客户端连接 */
static void handle_client(void *arg) {
    struct client_arg *ca = arg;
    int client_fd = ca->fd;
    const char *root_dir = ca->root_dir;
    char buffer[BUFFER_SIZE];

    while (g_running) {
        /* 读取请求 */
        int n = coco_read(client_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) break;

        buffer[n] = '\0';

        /* 解析请求 */
        http_request_t req;
        if (parse_http_request(buffer, n, &req) < 0) {
            send_error_response(client_fd, 400, "Bad Request");
            break;
        }

        /* 只支持 GET 和 HEAD */
        if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
            send_error_response(client_fd, 405, "Method Not Allowed");
            if (!req.keep_alive) break;
            continue;
        }

        /* URL 解码 */
        char decoded_path[MAX_PATH];
        if (url_decode(req.path, decoded_path, sizeof(decoded_path)) < 0) {
            send_error_response(client_fd, 400, "Bad Request");
            break;
        }

        /* 服务请求 */
        serve_path(client_fd, root_dir, decoded_path,
                   strcmp(req.method, "HEAD") == 0);

        /* 检查 keep-alive */
        if (!req.keep_alive) break;
    }

    close(client_fd);
    free(ca);
}

/* 接受连接循环 */
struct accept_arg {
    coco_sched_t *sched;
    int listen_fd;
    char root_dir[PATH_MAX];
};

static int set_nonblock(int fd);

static void accept_loop(void *arg) {
    struct accept_arg *aa = arg;
    coco_sched_t *sched = aa->sched;
    int listen_fd = aa->listen_fd;
    const char *root_dir = aa->root_dir;

    while (g_running) {
        struct sockaddr_in client_addr;
        size_t addrlen = sizeof(client_addr);

        int client_fd = coco_accept(listen_fd, &client_addr, &addrlen);
        if (client_fd < 0) {
            continue;
        }

        set_nonblock(client_fd);

        /* 为每个客户端创建新协程 */
        struct client_arg *ca = malloc(sizeof(*ca));
        if (ca) {
            ca->sched = sched;
            ca->fd = client_fd;
            strncpy(ca->root_dir, root_dir, PATH_MAX - 1);
            coco_create(sched, handle_client, ca, 0);
        } else {
            close(client_fd);
        }
    }
}

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    /* 关闭监听 socket 以中断 coco_accept() */
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
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

    g_listen_fd = listen_fd;

    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        close(listen_fd);
        return 1;
    }

    /* 启动 accept 协程 */
    struct accept_arg *aa = malloc(sizeof(*aa));
    if (!aa) {
        fprintf(stderr, "Failed to allocate accept arg\n");
        coco_sched_destroy(sched);
        close(listen_fd);
        return 1;
    }
    aa->sched = sched;
    aa->listen_fd = listen_fd;
    strncpy(aa->root_dir, resolved_root, PATH_MAX - 1);
    coco_create(sched, accept_loop, aa, 0);

    coco_sched_run(sched);

    coco_sched_destroy(sched);
    close(listen_fd);
    printf("Server stopped\n");
    return 0;
}