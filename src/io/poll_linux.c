/**
 * poll_linux.c - Linux I/O 多路复用实现
 *
 * 自动选择 io_uring (Linux 5.1+) 或 epoll 作为后端。
 * io_uring 提供更高性能，epoll 作为兼容回退。
 */

#include "../coco_internal.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/utsname.h>

/* 外部全局变量（TLS） */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

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
 * 自动选择 io_uring (Linux 5.1+) 或 epoll。
 */
int coco_poll_init(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* 尝试 io_uring (Linux 5.1+) */
    if (kernel_version_at_least(5, 1)) {
        if (coco_poll_init_iouring(sched) == COCO_OK) {
            return COCO_OK;
        }
        /* io_uring 初始化失败，回退 epoll */
    }

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

    /* epoll 注册 */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return COCO_ERROR;
    }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

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
    struct epoll_event events[64];
    int n = epoll_wait(sched->poll_fd, events, 64, timeout_ms);

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