/**
 * poll_linux.c - Linux I/O 多路复用实现
 *
 * 自动选择 io_uring (Linux 5.1+) 或 epoll 作为后端。
 * io_uring 提供更高性能，epoll 作为兼容回退。
 */

#include "../coco_internal.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/utsname.h>

/* 外部全局变量（TLS） */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/* io_uring 后端内部函数声明 */
#ifdef __linux__
extern coco_batch_io_t *coco_batch_begin_iouring(coco_sched_t *sched);
extern int coco_batch_add_read_iouring(coco_batch_io_t *batch, int fd, void *buf, size_t count);
extern int coco_batch_add_write_iouring(coco_batch_io_t *batch, int fd, const void *buf, size_t count);
extern int coco_batch_submit_iouring(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results);
extern int coco_batch_cancel_iouring(coco_batch_io_t *batch);
extern void coco_batch_end_iouring(coco_batch_io_t *batch);
extern void coco_iouring_get_stats_internal(coco_sched_t *sched, uint64_t *submit_count, uint64_t *syscall_count);
#endif

/* 检测内核版本 */
static bool kernel_version_at_least(int major, int minor) {
    struct utsname uts;
    if (uname(&uts) != 0) return false;

    int kmajor = 0, kminor = 0, kpatch = 0;
    sscanf(uts.release, "%d.%d.%d", &kmajor, &kminor, &kpatch);

    if (kmajor > major) return true;
    if (kmajor == major && kminor > minor) return true;
    if (kmajor == major && kminor == minor) return true;
    return false;
}

/**
 * coco_poll_init - 初始化 I/O 多路复用
 *
 * 根据配置选择 io_uring 或 epoll。
 */
int coco_poll_init(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* 初始化配置 */
    sched->poll_config.backend_forced = false;
    sched->poll_config.forced_backend = COCO_IO_BACKEND_AUTO;

    /* 检查是否强制选择后端 */
    if (sched->poll_config.backend_forced) {
        if (sched->poll_config.forced_backend == COCO_IO_BACKEND_IOURING) {
            /* 强制 io_uring */
            if (!kernel_version_at_least(5, 1)) {
                return COCO_ERROR;  /* 内核版本不支持 */
            }
            return coco_poll_init_iouring(sched);
        } else if (sched->poll_config.forced_backend == COCO_IO_BACKEND_EPOLL) {
            /* 强制 epoll */
            goto use_epoll;
        }
    }

    /* 自动选择：尝试 io_uring (Linux 5.1+) */
    if (kernel_version_at_least(5, 1)) {
        if (coco_poll_init_iouring(sched) == COCO_OK) {
            return COCO_OK;
        }
        /* io_uring 初始化失败，回退 epoll */
    }

use_epoll:
    /* 使用 epoll */
    sched->poll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (sched->poll_fd < 0) {
        return COCO_ERROR;
    }

    sched->poll_backend = COCO_POLL_EPOLL;

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
 */
int coco_sched_set_io_backend(coco_sched_t *sched, coco_io_backend_t backend) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* 必须在调度器初始化之前调用 */
    if (sched->poll_fd >= 0 || sched->iouring) {
        return COCO_ERROR;  /* 已初始化，无法更改 */
    }

    sched->poll_config.forced_backend = backend;
    sched->poll_config.backend_forced = (backend != COCO_IO_BACKEND_AUTO);

    return COCO_OK;
}

/**
 * coco_sched_get_io_backend - 获取当前 I/O 后端
 */
coco_io_backend_t coco_sched_get_io_backend(coco_sched_t *sched) {
    if (!sched) {
        return COCO_IO_BACKEND_AUTO;
    }

    switch (sched->poll_backend) {
        case COCO_POLL_IOURING:
            return COCO_IO_BACKEND_IOURING;
        case COCO_POLL_EPOLL:
            return COCO_IO_BACKEND_EPOLL;
        default:
            return COCO_IO_BACKEND_AUTO;
    }
}

/**
 * coco_poll_cleanup - 清理 I/O 多路复用资源
 */
void coco_poll_cleanup(coco_sched_t *sched) {
    if (!sched) return;

    if (sched->poll_backend == COCO_POLL_IOURING) {
        coco_poll_cleanup_iouring(sched);
        return;
    }

    /* epoll 清理 */
    if (sched->poll_fd >= 0) {
        close(sched->poll_fd);
        sched->poll_fd = -1;
    }
    if (sched->fd_table) {
        fd_table_destroy(sched->fd_table);
        sched->fd_table = NULL;
    }
}

/**
 * coco_poll_register - 注册 fd 事件
 */
int coco_poll_register(coco_sched_t *sched, int fd, coco_coro_t *coro, short events) {
    if (!sched || fd < 0) {
        return COCO_ERROR;
    }

    if (sched->poll_backend == COCO_POLL_IOURING) {
        return coco_poll_register_iouring(sched, fd, coro, events);
    }

    /* epoll 注册 - 使用 O_NONBLOCK 缓存 */
    if (fd < 32 && coro && (coro->nonblock_fds_set & (1U << fd))) {
        /* 已设置，跳过 fcntl */
    } else {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            return COCO_ERROR;
        }
        if (!(flags & O_NONBLOCK)) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
        if (fd < 32 && coro) {
            coro->nonblock_fds_set |= (1U << fd);
        }
    }

    struct epoll_event ev;
    ev.events = events | EPOLLONESHOT | EPOLLET;
    ev.data.fd = fd;

    if (epoll_ctl(sched->poll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return COCO_ERROR;
    }

    if (fd_table_set(sched->fd_table, fd, coro) < 0) {
        return COCO_ERROR;
    }
    coro->wait_fd = fd;
    coro->state = COCO_STATE_WAITING;

    return COCO_OK;
}

/**
 * coco_poll_unregister - 注销 fd 事件
 */
void coco_poll_unregister(coco_sched_t *sched, int fd) {
    if (!sched || fd < 0) return;

    if (sched->poll_backend == COCO_POLL_IOURING) {
        coco_poll_unregister_iouring(sched, fd);
        return;
    }

    /* 清除 O_NONBLOCK 缓存 */
    coco_coro_t *coro = g_current_coro;
    if (fd < 32 && coro) {
        coro->nonblock_fds_set &= ~(1U << fd);
    }

    epoll_ctl(sched->poll_fd, EPOLL_CTL_DEL, fd, NULL);
    fd_table_clear(sched->fd_table, fd);
}

/**
 * coco_poll_wait - 等待 I/O 事件
 */
int coco_poll_wait(coco_sched_t *sched, int timeout_ms) {
    if (!sched) return 0;

    if (sched->poll_backend == COCO_POLL_IOURING) {
        return coco_poll_wait_iouring(sched, timeout_ms);
    }

    /* epoll 等待 */
    struct epoll_event events[COCO_EPOLL_MAX_EVENTS];
    int n = epoll_wait(sched->poll_fd, events, COCO_EPOLL_MAX_EVENTS, timeout_ms);

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        coco_coro_t *coro = fd_table_get(sched->fd_table, fd);

        if (coro && coro->state == COCO_STATE_WAITING) {
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
        coco_poll_register(sched, fd, coro, EPOLLOUT);
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

/* === 批量 I/O API === */

coco_batch_io_t *coco_batch_begin(coco_sched_t *sched) {
    if (!sched) return NULL;
#ifdef __linux__
    if (sched->iouring) {
        return coco_batch_begin_iouring(sched);
    }
#endif
    return NULL;  /* epoll 不支持批量 I/O */
}

int coco_batch_add_read(coco_batch_io_t *batch, int fd, void *buf, size_t count) {
    if (!batch) return COCO_ERROR;
#ifdef __linux__
    return coco_batch_add_read_iouring(batch, fd, buf, count);
#else
    (void)fd; (void)buf; (void)count;
    return COCO_ERROR;
#endif
}

int coco_batch_add_write(coco_batch_io_t *batch, int fd, const void *buf, size_t count) {
    if (!batch) return COCO_ERROR;
#ifdef __linux__
    return coco_batch_add_write_iouring(batch, fd, buf, count);
#else
    (void)fd; (void)buf; (void)count;
    return COCO_ERROR;
#endif
}

int coco_batch_submit(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results) {
    if (!batch) return COCO_ERROR;
#ifdef __linux__
    return coco_batch_submit_iouring(batch, results, max_results);
#else
    (void)results; (void)max_results;
    return COCO_ERROR;
#endif
}

int coco_batch_cancel(coco_batch_io_t *batch) {
    if (!batch) return COCO_ERROR;
#ifdef __linux__
    return coco_batch_cancel_iouring(batch);
#else
    return COCO_ERROR;
#endif
}

void coco_batch_end(coco_batch_io_t *batch) {
    if (!batch) return;
#ifdef __linux__
    coco_batch_end_iouring(batch);
#endif
}

/* === I/O 配置 API === */

int coco_sched_set_io_options(coco_sched_t *sched, const coco_io_options_t *options) {
    if (!sched || !options) {
        return COCO_ERROR;
    }

    /* 必须在调度器初始化之前调用 */
    if (sched->poll_fd >= 0) {
        return COCO_ERROR;
    }

    sched->io_options = *options;
    sched->io_options_set = true;

    return COCO_OK;
}

int coco_sched_get_io_options(coco_sched_t *sched, coco_io_options_t *options) {
    if (!sched || !options) {
        return COCO_ERROR;
    }

    /* epoll 后端返回默认配置 */
    options->queue_depth = 256;
    options->sqpoll_enabled = false;
    options->sqpoll_cpu = -1;
    options->sqpoll_idle_ms = 0;

    if (sched->io_options_set) {
        *options = sched->io_options;
    }

    return COCO_OK;
}

void coco_iouring_get_stats(coco_sched_t *sched, uint64_t *submit_count, uint64_t *syscall_count) {
    if (!sched) {
        if (submit_count) *submit_count = 0;
        if (syscall_count) *syscall_count = 0;
        return;
    }
#ifdef __linux__
    if (sched->iouring) {
        coco_iouring_get_stats_internal(sched, submit_count, syscall_count);
        return;
    }
#endif
    if (submit_count) *submit_count = 0;
    if (syscall_count) *syscall_count = 0;
}