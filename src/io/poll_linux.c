/**
 * poll_linux.c - epoll 事件循环实现
 *
 * Linux 使用 epoll 进行 I/O 多路复用。
 */

#include "../coco_internal.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* fd 到协程的映射表 */
#define FD_TABLE_SIZE 1024
static coco_coro_t *g_fd_table[FD_TABLE_SIZE];

/* 外部全局变量（在 coro.c 中定义） */
extern coco_sched_t *g_current_sched;
extern coco_coro_t *g_current_coro;

/**
 * coco_poll_init - 初始化 epoll 实例
 *
 * @param sched 调度器
 * @return 0 成功，负数错误码
 */
int coco_poll_init(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* 创建 epoll 实例 */
    sched->poll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (sched->poll_fd < 0) {
        return COCO_ERROR;
    }

    /* 初始化 fd 映射表 */
    memset(g_fd_table, 0, sizeof(g_fd_table));

    return COCO_OK;
}

/**
 * coco_poll_cleanup - 清理 epoll 实例
 */
void coco_poll_cleanup(coco_sched_t *sched) {
    if (sched && sched->poll_fd >= 0) {
        close(sched->poll_fd);
        sched->poll_fd = -1;
    }
}

/**
 * coco_poll_register - 注册 fd 事件
 *
 * @param sched 调度器
 * @param fd 文件描述符
 * @param coro 协程
 * @param events 事件类型 (EPOLLIN/EPOLLOUT)
 * @return 0 成功，负数错误码
 */
int coco_poll_register(coco_sched_t *sched, int fd, coco_coro_t *coro, short events) {
    if (!sched || sched->poll_fd < 0 || fd < 0 || fd >= FD_TABLE_SIZE) {
        return COCO_ERROR;
    }

    /* 设置 fd 为非阻塞 */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return COCO_ERROR;
    }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* 创建 epoll_event 结构 */
    struct epoll_event ev;
    ev.events = events | EPOLLONESHOT | EPOLLET;  /* 边缘触发 + one-shot */
    ev.data.fd = fd;

    /* 注册到 epoll */
    if (epoll_ctl(sched->poll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return COCO_ERROR;
    }

    /* 映射 fd 到协程 */
    g_fd_table[fd] = coro;
    coro->wait_fd = fd;
    coro->state = COCO_STATE_WAITING;

    return COCO_OK;
}

/**
 * coco_poll_unregister - 注销 fd 事件
 *
 * @param sched 调度器
 * @param fd 文件描述符
 */
void coco_poll_unregister(coco_sched_t *sched, int fd) {
    if (!sched || sched->poll_fd < 0 || fd < 0 || fd >= FD_TABLE_SIZE) {
        return;
    }

    epoll_ctl(sched->poll_fd, EPOLL_CTL_DEL, fd, NULL);
    g_fd_table[fd] = NULL;
}

/**
 * coco_poll_wait - 等待 I/O 事件
 *
 * @param sched 度器
 * @param timeout_ms 超时时间（毫秒），0 表示不等待，-1 表示无限等待
 * @return 就绪事件数量
 */
int coco_poll_wait(coco_sched_t *sched, int timeout_ms) {
    if (!sched || sched->poll_fd < 0) {
        return 0;
    }

    struct epoll_event events[64];
    int n = epoll_wait(sched->poll_fd, events, 64, timeout_ms);

    /* 处理就绪事件 */
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        coco_coro_t *coro = g_fd_table[fd];

        if (coro && coro->state == COCO_STATE_WAITING) {
            /* 唤醒协程 */
            enqueue_ready(sched, coro);
            g_fd_table[fd] = NULL;
            coro->wait_fd = -1;
        }
    }

    return n;
}

/**
 * coco_read - 协程读取（阻塞）
 *
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 * @return 实际读取字节数，负数错误码
 */
int coco_read(int fd, void *buf, size_t count) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    while (1) {
        ssize_t n = read(fd, buf, count);

        if (n >= 0) {
            return (int)n;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 注册读事件并等待 */
            coco_poll_register(sched, fd, coro, EPOLLIN);
            coco_yield();
            coco_poll_unregister(sched, fd);
        } else {
            return COCO_ERROR;
        }
    }
}

/**
 * coco_write - 协程写入（阻塞）
 *
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 写入字节数
 * @return 实际写入字节数，负数错误码
 */
int coco_write(int fd, const void *buf, size_t count) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    size_t written = 0;

    while (written < count) {
        ssize_t n = write(fd, buf + written, count - written);

        if (n >= 0) {
            written += n;
            if (written >= count) {
                return (int)written;
            }
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 注册写事件并等待 */
            coco_poll_register(sched, fd, coro, EPOLLOUT);
            coco_yield();
            coco_poll_unregister(sched, fd);
        } else {
            return COCO_ERROR;
        }
    }

    return (int)written;
}

/**
 * coco_accept - 协程 accept（阻塞）
 *
 * @param fd 监听 socket
 * @param addr 客户端地址
 * @param addrlen 地址长度
 * @return 新 socket，负数错误码
 */
int coco_accept(int fd, void *addr, size_t *addrlen) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    while (1) {
        int client_fd = accept(fd, (struct sockaddr*)addr, (socklen_t*)addrlen);

        if (client_fd >= 0) {
            return client_fd;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 注册读事件并等待 */
            coco_poll_register(sched, fd, coro, EPOLLIN);
            coco_yield();
            coco_poll_unregister(sched, fd);
        } else {
            return COCO_ERROR;
        }
    }
}

/**
 * coco_connect - 协程 connect（阻塞）
 *
 * @param fd socket
 * @param addr 目标地址
 * @param addrlen 地址长度
 * @return 0 成功，负数错误码
 */
int coco_connect(int fd, const void *addr, size_t addrlen) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    /* 尝试连接 */
    int ret = connect(fd, (const struct sockaddr*)addr, (socklen_t)addrlen);

    if (ret == 0) {
        return COCO_OK;
    }

    if (errno == EINPROGRESS) {
        /* 非阻塞连接，等待写事件 */
        coco_poll_register(sched, fd, coro, EPOLLOUT);
        coco_yield();
        coco_poll_unregister(sched, fd);

        /* 检查连接结果 */
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);

        if (error == 0) {
            return COCO_OK;
        } else {
            return COCO_ERROR;
        }
    }

    return COCO_ERROR;
}