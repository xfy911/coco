/**
 * poll_windows.c - WSAPoll I/O 多路复用实现
 *
 * Windows 使用 WSAPoll 进行 I/O 多路复用。
 * WSAPoll 的行为与 POSIX poll() 类似，但仅适用于 socket。
 *
 * 限制：
 * - FD_SETSIZE 在 Windows 上默认为 64，可通过定义 FD_SETSIZE
 *   宏在包含 winsock2.h 之前增大。本实现使用 fd_table 跟踪
 *   协程映射，不受 FD_SETSIZE 限制，但 WSAPoll 调用的
 *   pollfd 数组大小受 COCO_WSAPOLL_MAX_EVENTS 约束。
 * - Windows 上 WSAPoll 仅支持 socket，不支持普通文件描述符。
 */

#ifdef _WIN32

#include "../coco_internal.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <string.h>

/* 外部全局变量（TLS） */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/* Winsock 初始化引用计数 */
static LONG g_ws2_init_count = 0;

/**
 * coco_poll_init - 初始化 WSAPoll 实例
 *
 * 调用 WSAStartup 初始化 Winsock2，创建 FD 表。
 * WSAPoll 不需要创建实例（与 epoll/kqueue 不同），
 * poll_fd 设为 0 表示已初始化。
 *
 * @param sched 调度器
 * @return 0 成功，负数错误码
 */
int coco_poll_init(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* 初始化 Winsock2（引用计数，支持多次调用） */
    if (InterlockedIncrement(&g_ws2_init_count) == 1) {
        WSADATA wsa_data;
        int ret = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (ret != 0) {
            InterlockedDecrement(&g_ws2_init_count);
            return COCO_ERROR;
        }
        /* 检查 Winsock 版本 */
        if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
            WSACleanup();
            InterlockedDecrement(&g_ws2_init_count);
            return COCO_ERROR;
        }
    }

    /* 初始化配置 */
    sched->poll_config.backend_forced = false;
    sched->poll_config.forced_backend = COCO_IO_BACKEND_AUTO;

    /* Windows 使用 WSAPoll */
    sched->poll_backend = COCO_POLL_WSAPOLL;

    /* WSAPoll 不需要创建实例，用 0 标记已初始化 */
    sched->poll_fd = 0;

    /* 创建 FD 表 */
    sched->fd_table = fd_table_create(1024);
    if (!sched->fd_table) {
        if (InterlockedDecrement(&g_ws2_init_count) == 0) {
            WSACleanup();
        }
        sched->poll_fd = -1;
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * coco_sched_set_io_backend - 设置 I/O 后端
 *
 * Windows 只支持 WSAPoll，其他后端请求返回错误。
 */
int coco_sched_set_io_backend(coco_sched_t *sched, coco_io_backend_t backend) {
    if (!sched) {
        return COCO_ERROR;
    }

    /* Windows 只支持 AUTO (WSAPoll) */
    if (backend != COCO_IO_BACKEND_AUTO) {
        return COCO_ERROR;  /* Windows 不支持 epoll/io_uring */
    }

    sched->poll_config.forced_backend = backend;
    sched->poll_config.backend_forced = false;

    return COCO_OK;
}

/**
 * coco_sched_get_io_backend - 获取当前 I/O 后端
 *
 * Windows 总是返回 AUTO (使用 WSAPoll)。
 */
coco_io_backend_t coco_sched_get_io_backend(coco_sched_t *sched) {
    if (!sched) {
        return COCO_IO_BACKEND_AUTO;
    }

    /* Windows 使用 WSAPoll，映射到 AUTO */
    return COCO_IO_BACKEND_AUTO;
}

/**
 * coco_poll_cleanup - 清理 WSAPoll 资源
 */
void coco_poll_cleanup(coco_sched_t *sched) {
    if (!sched) return;

    if (sched->fd_table) {
        fd_table_destroy(sched->fd_table);
        sched->fd_table = NULL;
    }

    sched->poll_fd = -1;

    /* 清理 Winsock（引用计数） */
    if (InterlockedDecrement(&g_ws2_init_count) == 0) {
        WSACleanup();
    }
}

/**
 * coco_poll_set_nonblock - 设置 socket 为非阻塞模式
 *
 * Windows 使用 ioctlsocket + FIONBIO 代替 fcntl。
 */
void coco_poll_set_nonblock(int fd) {
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
}

/**
 * coco_poll_register - 注册 fd 事件
 *
 * @param sched 调度器
 * @param fd socket 描述符
 * @param coro 协程
 * @param events 事件类型 (POLLIN/POLLOUT)
 * @return 0 成功，负数错误码
 */
int coco_poll_register(coco_sched_t *sched, int fd, coco_coro_t *coro, short events) {
    if (!sched || fd < 0) {
        return COCO_ERROR;
    }

    /* 设置 socket 为非阻塞 (使用缓存) */
    if (fd < 32 && coro && (coro->nonblock_fds_set & (1U << fd))) {
        /* 已设置，跳过 ioctlsocket */
    } else {
        coco_poll_set_nonblock(fd);
        if (fd < 32 && coro) {
            coro->nonblock_fds_set |= (1U << fd);
        }
    }

    /* WSAPoll 不需要预先注册 fd，只需在 fd_table 中映射 */
    /* fd 的事件类型在 coco_poll_wait 中从 fd_table 重建 */
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
 * @param fd socket 描述符
 */
void coco_poll_unregister(coco_sched_t *sched, int fd) {
    if (!sched || fd < 0) return;

    /* 清除非阻塞缓存 */
    coco_coro_t *coro = g_current_coro;
    if (fd < 32 && coro) {
        coro->nonblock_fds_set &= ~(1U << fd);
    }

    fd_table_clear(sched->fd_table, fd);
}

/**
 * coco_poll_wait - 等待 I/O 事件
 *
 * 使用 WSAPoll 等待已注册的 socket 事件。
 * 需要遍历 fd_table 构建 pollfd 数组。
 *
 * @param sched 调度器
 * @param timeout_ms 超时时间（毫秒），0 表示不等待，-1 表示无限等待
 * @return 就绪事件数量
 */
int coco_poll_wait(coco_sched_t *sched, int timeout_ms) {
    if (!sched || !sched->fd_table) {
        return 0;
    }

    /* 构建 pollfd 数组 */
    struct pollfd fds[COCO_WSAPOLL_MAX_EVENTS];
    int nfds = 0;
    fd_table_t *ft = sched->fd_table;

    /* 遍历 fd_table 中已注册的 fd */
    uint32_t max_fd = ft->max_fd;
    if (max_fd >= ft->capacity) {
        max_fd = ft->capacity - 1;
    }

    for (uint32_t i = 0; i <= max_fd && nfds < COCO_WSAPOLL_MAX_EVENTS; i++) {
        coco_coro_t *coro = ft->table[i];
        if (coro && coro->state == COCO_STATE_WAITING) {
            fds[nfds].fd = (SOCKET)i;
            /* 根据协程等待的 fd 确定事件类型：
             * accept/read 用 POLLIN，connect/write 用 POLLOUT */
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }
    }

    if (nfds == 0) {
        return 0;
    }

    /* 调用 WSAPoll */
    int n = WSAPoll(fds, (ULONG)nfds, timeout_ms);
    if (n <= 0) {
        return 0;
    }

    /* 处理就绪事件 */
    int ready_count = 0;
    for (int i = 0; i < nfds && ready_count < n; i++) {
        if (fds[i].revents & (POLLIN | POLLOUT | POLLHUP | POLLERR)) {
            int fd = fds[i].fd;
            coco_coro_t *coro = fd_table_get(sched->fd_table, fd);

            if (coro && coro->state == COCO_STATE_WAITING) {
                enqueue_ready(sched, coro);
                fd_table_clear(sched->fd_table, fd);
                coro->wait_fd = -1;
                ready_count++;
            }
        }
    }

    return ready_count;
}

/**
 * coco_read - 协程读取（阻塞）
 *
 * @param fd socket 描述符
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

    /* 确保 socket 为非阻塞模式 */
    if (fd < 32 && (coro->nonblock_fds_set & (1U << fd))) {
        /* 已设置 */
    } else {
        coco_poll_set_nonblock(fd);
        if (fd < 32) {
            coro->nonblock_fds_set |= (1U << fd);
        }
    }

    while (1) {
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        int n = recv(fd, (char *)buf, (int)count, 0);

        if (n >= 0) {
            return n;
        }

        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
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
 * @param fd socket 描述符
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

    /* 确保 socket 为非阻塞模式 */
    if (fd < 32 && (coro->nonblock_fds_set & (1U << fd))) {
        /* 已设置 */
    } else {
        coco_poll_set_nonblock(fd);
        if (fd < 32) {
            coro->nonblock_fds_set |= (1U << fd);
        }
    }

    size_t written = 0;

    while (written < count) {
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        int n = send(fd, (const char *)buf + written, (int)(count - written), 0);

        if (n >= 0) {
            written += n;
            if (written >= count) {
                return (int)written;
            }
        }

        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
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

    /* 确保 socket 为非阻塞模式 */
    if (fd < 32 && (coro->nonblock_fds_set & (1U << fd))) {
        /* 已设置 */
    } else {
        coco_poll_set_nonblock(fd);
        if (fd < 32) {
            coro->nonblock_fds_set |= (1U << fd);
        }
    }

    while (1) {
        /* 检查取消状态 */
        if (coro->cancelled) {
            return COCO_ERROR_CANCELLED;
        }

        int client_fd = (int)accept(fd, (struct sockaddr *)addr, (socklen_t *)addrlen);

        if (client_fd >= 0) {
            return client_fd;
        }

        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
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

    /* 确保 socket 为非阻塞模式 */
    if (fd < 32 && (coro->nonblock_fds_set & (1U << fd))) {
        /* 已设置 */
    } else {
        coco_poll_set_nonblock(fd);
        if (fd < 32) {
            coro->nonblock_fds_set |= (1U << fd);
        }
    }

    /* 检查取消状态 */
    if (coro->cancelled) {
        return COCO_ERROR_CANCELLED;
    }

    /* 尝试连接 */
    int ret = connect(fd, (const struct sockaddr *)addr, (socklen_t)addrlen);

    if (ret == 0) {
        return COCO_OK;
    }

    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK) {
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
        int len = sizeof(error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&error, &len);

        if (error == 0) {
            return COCO_OK;
        } else {
            return COCO_ERROR;
        }
    }

    return COCO_ERROR;
}

/* === 批量 I/O API (WSAPoll 不支持) === */

coco_batch_io_t *coco_batch_begin(coco_sched_t *sched) {
    (void)sched;
    return NULL;  /* WSAPoll 不支持批量 I/O */
}

int coco_batch_add_read(coco_batch_io_t *batch, int fd, void *buf, size_t count) {
    (void)batch; (void)fd; (void)buf; (void)count;
    return COCO_ERROR;
}

int coco_batch_add_write(coco_batch_io_t *batch, int fd, const void *buf, size_t count) {
    (void)batch; (void)fd; (void)buf; (void)count;
    return COCO_ERROR;
}

int coco_batch_submit(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results) {
    (void)batch; (void)results; (void)max_results;
    return COCO_ERROR;
}

int coco_batch_cancel(coco_batch_io_t *batch) {
    (void)batch;
    return COCO_ERROR;
}

void coco_batch_end(coco_batch_io_t *batch) {
    (void)batch;
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

    /* WSAPoll 后端返回默认配置 */
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
    (void)sched;
    if (submit_count) *submit_count = 0;
    if (syscall_count) *syscall_count = 0;
}

#endif /* _WIN32 */
