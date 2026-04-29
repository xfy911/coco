/**
 * poll_macos.c - kqueue 事件循环实现
 *
 * macOS 使用 kqueue 进行 I/O 多路复用。
 */

#include "../coco_internal.h"
#include <sys/event.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* 外部全局变量（TLS） */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/**
 * coco_poll_init - 初始化 kqueue 实例
 *
 * @param sched 调度器
 * @return 0 成功，负数错误码
 */
int coco_poll_init(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* 初始化配置 */
    sched->poll_config.backend_forced = false;
    sched->poll_config.forced_backend = COCO_IO_BACKEND_AUTO;

    /* macOS 只支持 kqueue */
    sched->poll_backend = COCO_POLL_KQUEUE;

    /* 创建 kqueue 实例 */
    sched->poll_fd = kqueue();
    if (sched->poll_fd < 0) {
        return COCO_ERROR;
    }

    /* 创建 FD 表 */
    sched->fd_table = fd_table_create(1024);
    if (!sched->fd_table) {
        close(sched->poll_fd);
        sched->poll_fd = -1;
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * coco_sched_set_io_backend - 设置 I/O 后端
 *
 * macOS 只支持 kqueue，其他后端请求返回错误。
 */
int coco_sched_set_io_backend(coco_sched_t *sched, coco_io_backend_t backend) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* macOS 只支持 AUTO (kqueue) */
    if (backend != COCO_IO_BACKEND_AUTO) {
        return COCO_ERROR;  /* macOS 不支持 epoll/io_uring */
    }

    sched->poll_config.forced_backend = backend;
    sched->poll_config.backend_forced = false;

    return COCO_OK;
}

/**
 * coco_sched_get_io_backend - 获取当前 I/O 后端
 *
 * macOS 总是返回 AUTO (使用 kqueue)。
 */
coco_io_backend_t coco_sched_get_io_backend(coco_sched_t *sched) {
    if (!sched) {
        return COCO_IO_BACKEND_AUTO;
    }

    /* macOS 使用 kqueue，映射到 AUTO */
    return COCO_IO_BACKEND_AUTO;
}

/**
 * coco_poll_cleanup - 清理 kqueue 实例
 */
void coco_poll_cleanup(coco_sched_t *sched) {
    if (sched) {
        if (sched->poll_fd >= 0) {
            close(sched->poll_fd);
            sched->poll_fd = -1;
        }
        if (sched->fd_table) {
            fd_table_destroy(sched->fd_table);
            sched->fd_table = NULL;
        }
    }
}

/**
 * coco_poll_register - 注册 fd 事件
 *
 * @param sched 调度器
 * @param fd 文件描述符
 * @param coro 协程
 * @param events 事件类型 (POLLIN/POLLOUT)
 * @return 0 成功，负数错误码
 */
int coco_poll_register(coco_sched_t *sched, int fd, coco_coro_t *coro, short events) {
    if (!sched || sched->poll_fd < 0 || fd < 0) {
        return COCO_ERROR;
    }

    /* 设置 fd 为非阻塞 */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return COCO_ERROR;
    }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* 创建 kevent 结构 */
    struct kevent kev;
    short filter = (events & POLLIN) ? EVFILT_READ : EVFILT_WRITE;

    EV_SET(&kev, fd, filter, EV_ADD | EV_ONESHOT, 0, 0, NULL);

    /* 注册到 kqueue */
    if (kevent(sched->poll_fd, &kev, 1, NULL, 0, NULL) < 0) {
        return COCO_ERROR;
    }

    /* 映射 fd 到协程 */
    if (fd_table_set(sched->fd_table, fd, coro) < 0) {
        return COCO_ERROR;
    }
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
    if (!sched || sched->poll_fd < 0 || fd < 0) {
        return;
    }

    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(sched->poll_fd, &kev, 1, NULL, 0, NULL);
    EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(sched->poll_fd, &kev, 1, NULL, 0, NULL);

    fd_table_clear(sched->fd_table, fd);
}

/**
 * coco_poll_wait - 等待 I/O 事件
 *
 * @param sched 调器
 * @param timeout_ms 超时时间（毫秒），0 表示不等待，-1 表示无限等待
 * @return 就绪事件数量
 */
int coco_poll_wait(coco_sched_t *sched, int timeout_ms) {
    if (!sched || sched->poll_fd < 0) {
        return 0;
    }

    struct timespec timeout;
    struct timespec *timeout_ptr = NULL;

    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (timeout_ms % 1000) * 1000000;
        timeout_ptr = &timeout;
    }

    struct kevent events[COCO_KQUEUE_MAX_EVENTS];
    int n = kevent(sched->poll_fd, NULL, 0, events, COCO_KQUEUE_MAX_EVENTS, timeout_ptr);

    /* 处理就绪事件 */
    for (int i = 0; i < n; i++) {
        int fd = events[i].ident;
        coco_coro_t *coro = fd_table_get(sched->fd_table, fd);

        if (coro && coro->state == COCO_STATE_WAITING) {
            /* 唤醒协程 */
            enqueue_ready(sched, coro);
            fd_table_clear(sched->fd_table, fd);
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
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        ssize_t n = read(fd, buf, count);

        if (n >= 0) {
            return (int)n;
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
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        ssize_t n = write(fd, buf + written, count - written);

        if (n >= 0) {
            written += n;
            if (written >= count) {
                return (int)written;
            }
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* 注册写事件并等待 */
            coco_poll_register(sched, fd, coro, POLLOUT);
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