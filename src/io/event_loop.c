/**
 * event_loop.c - 事件循环核心 + 平台无关 I/O 操作
 *
 * coco_poll_set_nonblock / coco_poll_wait_ready 由各平台文件提供。
 * coco_read / coco_write / coco_accept / coco_connect 在此实现，
 * 通过上述平台抽象实现跨平台复用。
 */

#include "../coco_internal.h"
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

/* 外部全局变量（TLS） */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/* === 平台无关 I/O 辅助 === */

/**
 * coco_poll_wait_ready - 注册 fd 事件并 yield 等待就绪
 *
 * @param fd 文件描述符
 * @param events 事件类型 (POLLIN/POLLOUT)
 */
void coco_poll_wait_ready(int fd, short events) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    coco_poll_set_nonblock(fd);
    coco_poll_register(sched, fd, coro, events);
    coco_yield();
    coco_poll_unregister(sched, fd);
}

/* === 协程 I/O 操作 === */

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

    coco_poll_set_nonblock(fd);

    while (1) {
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        ssize_t n = read(fd, buf, count);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 注册读事件并等待 */
                coco_poll_register(sched, fd, coro, POLLIN);
                coco_yield();
                coco_poll_unregister(sched, fd);
            } else {
                return COCO_ERROR;
            }
        } else {
            return (int)n;
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

    coco_poll_set_nonblock(fd);

    size_t written = 0;

    while (written < count) {
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        ssize_t n = write(fd, buf + written, count - written);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 注册写事件并等待 */
                coco_poll_register(sched, fd, coro, POLLOUT);
                coco_yield();
                coco_poll_unregister(sched, fd);
            } else {
                return COCO_ERROR;
            }
        } else {
            written += n;
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

    coco_poll_set_nonblock(fd);

    while (1) {
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        int client_fd = accept(fd, (struct sockaddr*)addr, (socklen_t*)addrlen);

        if (client_fd >= 0) {
            return client_fd;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 注册读事件并等待 */
            coco_poll_register(sched, fd, coro, POLLIN);
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

    coco_poll_set_nonblock(fd);

    /* 检查取消状态 */
    if (coro->cancelled) {
        return COCO_ERROR_CANCELLED;
    }

    /* 尝试连接 */
    int ret = connect(fd, (const struct sockaddr*)addr, (socklen_t)addrlen);

    if (ret == 0) {
        return COCO_OK;
    }

    if (errno == EINPROGRESS) {
        /* 非阻塞连接，等待写事件 */
        coco_poll_register(sched, fd, coro, POLLOUT);
        coco_yield();
        coco_poll_unregister(sched, fd);

        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

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

/* === Sleep === */

int coco_sleep(uint64_t ms) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    /* 注册定时器 */
    coco_timer_add(sched->timer_wheel, ms, coro);

    /* 设置为等待状态并 yield */
    coro->state = COCO_STATE_WAITING;
    coco_yield();

    return COCO_OK;
}
