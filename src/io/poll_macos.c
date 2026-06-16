/**
 * poll_macos.c - kqueue 事件循环实现
 *
 * macOS 使用 kqueue 进行 I/O 多路复用。
 *
 * 仅包含 poll 层: init, cleanup, register, unregister, wait,
 * set_nonblock 及 I/O 后端配置。
 * coco_read/write/accept/connect 已移至 event_loop.c。
 */

#include "../coco_internal.h"
#include "io_internal.h"
#include <sys/event.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* 外部全局变量（TLS） */

/* === 平台抽象: 非阻塞设置 === */

/**
 * coco_poll_set_nonblock - 设置 fd 为非阻塞模式
 */
void coco_poll_set_nonblock(int fd) {
    if (fd_table_is_nonblock(fd)) {
        return;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1 && !(flags & O_NONBLOCK)) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    fd_table_mark_nonblock(fd);
}

/* === Poll 层实现 === */

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

    /* 设置 fd 为非阻塞 (使用缓存) */
    if (!fd_table_is_nonblock(fd)) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            return COCO_ERROR;
        }
        if (!(flags & O_NONBLOCK)) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
        fd_table_mark_nonblock(fd);
    }

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
    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);

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

    /* kqueue 注销 */
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
 * @param sched 调度器
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

        if (coro && atomic_load_explicit(&coro->state, memory_order_acquire) == COCO_STATE_WAITING) {
            /* 唤醒协程 */
            enqueue_ready(sched, coro);
            fd_table_clear(sched->fd_table, fd);
            coro->wait_fd = -1;
        }
    }

    return n;
}

/* === I/O 配置 API === */

int coco_sched_set_io_options(coco_sched_t *sched, const coco_io_options_t *options) {
    return coco_sched_set_io_options_impl(sched, options);
}

int coco_sched_get_io_options(coco_sched_t *sched, coco_io_options_t *options) {
    return coco_sched_get_io_options_impl(sched, options);
}

void coco_iouring_get_stats(coco_sched_t *sched, uint64_t *submit_count, uint64_t *syscall_count) {
    (void)sched;
    coco_iouring_get_stats_default(submit_count, syscall_count);
}
