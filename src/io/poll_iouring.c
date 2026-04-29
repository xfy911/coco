/**
 * poll_iouring.c - io_uring 事件循环实现
 *
 * Linux 5.1+ 使用 io_uring 进行高性能 I/O 多路复用。
 * 支持批量提交、SQPOLL 轮询、I/O 取消和文件 I/O。
 */

#include "../coco_internal.h"

#ifdef __linux__

#include <liburing.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* io_uring 配置 */
#define IOURING_ENTRIES     256   /* 默认队列深度 */
#define IOURING_SQPOLL_IDLE 1000  /* SQPOLL 空闲超时 (ms) */

/* io_uring 请求类型 */
typedef enum {
    IOURING_REQ_POLL,     /* 轮询请求 (网络 I/O) */
    IOURING_REQ_READ,     /* 文件读取 */
    IOURING_REQ_WRITE,    /* 文件写入 */
    IOURING_REQ_OPEN,     /* 文件打开 */
    IOURING_REQ_CLOSE,    /* 文件关闭 */
    IOURING_REQ_CANCEL    /* 取消请求 */
} iouring_req_type_t;

/* io_uring 请求结构 */
typedef struct iouring_req {
    iouring_req_type_t type;    /* 请求类型 */
    coco_coro_t *coro;          /* 关联协程 */
    int fd;                     /* 文件描述符 */
    short events;               /* 监听事件 (POLLIN/POLLOUT) */
    bool cancelled;             /* 取消标志 */

    /* 文件 I/O 相关 */
    void *buf;                  /* 缓冲区 */
    size_t count;               /* 字节数 */
    ssize_t result;             /* 结果 */

    struct iouring_req *next;   /* 链表下一个 */
} iouring_req_t;

/* io_uring 上下文 */
typedef struct coco_iouring {
    struct io_uring ring;           /* io_uring 实例 */
    struct io_uring_params params;  /* 配置参数 */
    bool sqpoll_enabled;            /* SQPOLL 是否启用 */
    uint32_t entries;               /* 队列深度 */

    /* 请求池 */
    iouring_req_t *req_freelist;    /* 空闲请求链表 */
    iouring_req_t req_pool[256];    /* 预分配请求池 */

    /* 统计 */
    uint64_t submit_count;          /* 提交次数 */
    uint64_t syscall_count;         /* 系统调用次数 */
} coco_iouring_t;

/* 线程局部 io_uring 上下文 */
static _Thread_local coco_iouring_t *g_iouring = NULL;

/* 从池中分配请求 */
static iouring_req_t *req_alloc(coco_iouring_t *iou) {
    if (iou && iou->req_freelist) {
        iouring_req_t *req = iou->req_freelist;
        iou->req_freelist = req->next;
        memset(req, 0, sizeof(iouring_req_t));
        return req;
    }
    return calloc(1, sizeof(iouring_req_t));
}

/* 归还请求到池 */
static void req_free(coco_iouring_t *iou, iouring_req_t *req) {
    if (!req) return;
    if (iou && req >= &iou->req_pool[0] && req <= &iou->req_pool[255]) {
        req->next = iou->req_freelist;
        iou->req_freelist = req;
    } else {
        free(req);
    }
}

/* 检测内核版本 */
static bool kernel_version_at_least(int major, int minor) {
    struct utsname uts;
    if (uname(&uts) != 0) return false;

    int kmajor = 0, kminor = 0, kpatch = 0;
    sscanf(uts.release, "%d.%d.%d", &kmajor, &kminor, &kpatch);

    if (kmajor > major) return true;
    if (kmajor == major && kminor > minor) return true;
    if (kmajor == major && kminor == minor && kpatch >= 0) return true;
    return false;
}

/* 检测 SQPOLL 支持 (Linux 5.11+) */
static bool sqpoll_supported(void) {
    return kernel_version_at_least(5, 11);
}

/**
 * coco_poll_init_iouring - 初始化 io_uring 实例
 *
 * @param sched 调度器
 * @return 0 成功，负数错误码
 */
int coco_poll_init_iouring(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = calloc(1, sizeof(coco_iouring_t));
    if (!iou) {
        return COCO_ERROR;
    }

    iou->entries = IOURING_ENTRIES;

    /* 初始化请求池 */
    for (int i = 0; i < 256; i++) {
        iou->req_pool[i].next = iou->req_freelist;
        iou->req_freelist = &iou->req_pool[i];
    }

    /* 尝试启用 SQPOLL */
    if (sqpoll_supported()) {
        memset(&iou->params, 0, sizeof(iou->params));
        iou->params.flags = IORING_SETUP_SQPOLL;
        iou->params.sq_thread_idle = IOURING_SQPOLL_IDLE;

        if (io_uring_queue_init_params(iou->entries, &iou->ring, &iou->params) == 0) {
            iou->sqpoll_enabled = true;
            sched->poll_fd = iou->ring.ring_fd;
            sched->poll_backend = COCO_POLL_IOURING;
            sched->iouring = iou;
            g_iouring = iou;

            /* 创建 FD 表 */
            sched->fd_table = fd_table_create(1024);
            if (!sched->fd_table) {
                io_uring_queue_exit(&iou->ring);
                free(iou);
                return COCO_ERROR;
            }

            return COCO_OK;
        }
        /* SQPOLL 失败，回退到默认模式 */
    }

    /* 默认模式初始化 */
    if (io_uring_queue_init(iou->entries, &iou->ring, 0) < 0) {
        free(iou);
        return COCO_ERROR;
    }

    sched->poll_fd = iou->ring.ring_fd;
    sched->poll_backend = COCO_POLL_IOURING;
    sched->iouring = iou;
    g_iouring = iou;

    /* 创建 FD 表 */
    sched->fd_table = fd_table_create(1024);
    if (!sched->fd_table) {
        io_uring_queue_exit(&iou->ring);
        free(iou);
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * coco_poll_cleanup_iouring - 清理 io_uring 实例
 */
void coco_poll_cleanup_iouring(coco_sched_t *sched) {
    if (!sched || !sched->iouring) return;

    coco_iouring_t *iou = sched->iouring;
    io_uring_queue_exit(&iou->ring);
    free(iou);
    sched->iouring = NULL;
    g_iouring = NULL;

    if (sched->fd_table) {
        fd_table_destroy(sched->fd_table);
        sched->fd_table = NULL;
    }
}

/**
 * coco_poll_register_iouring - 注册 fd 事件
 */
int coco_poll_register_iouring(coco_sched_t *sched, int fd, coco_coro_t *coro, short events) {
    if (!sched || !sched->iouring || fd < 0) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;

    /* 设置 fd 为非阻塞 */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return COCO_ERROR;
    }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* 分配请求 */
    iouring_req_t *req = req_alloc(iou);
    if (!req) {
        return COCO_ERROR;
    }

    req->type = IOURING_REQ_POLL;
    req->coro = coro;
    req->fd = fd;
    req->events = events;
    req->cancelled = false;

    /* 获取 SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iou->ring);
    if (!sqe) {
        req_free(iou, req);
        return COCO_ERROR;
    }

    /* 准备 POLL 操作 */
    io_uring_prep_poll_add(sqe, fd, events);
    io_uring_sqe_set_data(sqe, req);

    /* 映射 fd 到协程 */
    if (fd_table_set(sched->fd_table, fd, coro) < 0) {
        req_free(iou, req);
        return COCO_ERROR;
    }

    coro->wait_fd = fd;
    coro->state = COCO_STATE_WAITING;

    return COCO_OK;
}

/**
 * coco_poll_unregister_iouring - 注销 fd 事件
 */
void coco_poll_unregister_iouring(coco_sched_t *sched, int fd) {
    if (!sched || !sched->iouring || fd < 0) {
        return;
    }

    /* io_uring 的 POLL 是 one-shot 的，无需显式注销 */
    fd_table_clear(sched->fd_table, fd);
}

/**
 * coco_poll_wait_iouring - 等待 I/O 事件
 */
int coco_poll_wait_iouring(coco_sched_t *sched, int timeout_ms) {
    if (!sched || !sched->iouring) {
        return 0;
    }

    coco_iouring_t *iou = sched->iouring;
    struct __kernel_timespec ts, *tsp = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
        tsp = &ts;
    }

    /* 提交待处理请求 */
    io_uring_submit(&iou->ring);
    iou->submit_count++;
    iou->syscall_count++;

    /* 等待完成事件 */
    struct io_uring_cqe *cqe = NULL;
    int ret = io_uring_wait_cqe_timeout(&iou->ring, &cqe, tsp);

    if (ret < 0) {
        if (ret == -ETIME || ret == -EINTR) {
            return 0;  /* 超时或中断 */
        }
        return COCO_ERROR;
    }

    int n = 0;

    /* 处理完成事件 */
    while (cqe) {
        iouring_req_t *req = (iouring_req_t *)io_uring_cqe_get_data(cqe);

        if (req && req->coro && !req->cancelled) {
            coco_coro_t *coro = req->coro;

            switch (req->type) {
                case IOURING_REQ_POLL:
                    /* 轮询完成，唤醒协程 */
                    if (coro->state == COCO_STATE_WAITING) {
                        enqueue_ready(sched, coro);
                        fd_table_clear(sched->fd_table, req->fd);
                        coro->wait_fd = -1;
                    }
                    break;

                case IOURING_REQ_READ:
                case IOURING_REQ_WRITE:
                    /* 文件 I/O 完成 */
                    req->result = cqe->res;
                    if (coro->state == COCO_STATE_WAITING) {
                        enqueue_ready(sched, coro);
                    }
                    break;

                case IOURING_REQ_CANCEL:
                    /* 取消完成 */
                    break;

                default:
                    break;
            }

            req_free(iou, req);
            n++;
        }

        io_uring_cqe_seen(&iou->ring, cqe);

        /* 尝试获取更多完成事件 */
        if (io_uring_peek_cqe(&iou->ring, &cqe) != 0) {
            cqe = NULL;
        }
    }

    return n;
}

/**
 * coco_poll_cancel_iouring - 取消 I/O 请求
 */
int coco_poll_cancel_iouring(coco_sched_t *sched, iouring_req_t *req) {
    if (!sched || !sched->iouring || !req) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;

    req->cancelled = true;

    /* 获取 SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iou->ring);
    if (!sqe) {
        return COCO_ERROR;
    }

    /* 准备取消操作 */
    io_uring_prep_cancel64(sqe, (uint64_t)req, 0);
    io_uring_sqe_set_data(sqe, NULL);

    return COCO_OK;
}

/* === 文件 I/O API === */

/**
 * coco_file_open - 异步打开文件
 */
int coco_file_open(const char *path, int flags, mode_t mode) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !sched->iouring || !coro) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;

    /* 分配请求 */
    iouring_req_t *req = req_alloc(iou);
    if (!req) {
        return COCO_ERROR;
    }

    req->type = IOURING_REQ_OPEN;
    req->coro = coro;

    /* 获取 SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iou->ring);
    if (!sqe) {
        req_free(iou, req);
        return COCO_ERROR;
    }

    /* 准备 OPEN 操作 */
    io_uring_prep_openat(sqe, AT_FDCWD, path, flags, mode);
    io_uring_sqe_set_data(sqe, req);

    coro->state = COCO_STATE_WAITING;

    /* 提交并等待 */
    io_uring_submit(&iou->ring);
    coco_yield();

    int result = (int)req->result;
    req_free(iou, req);

    return result;
}

/**
 * coco_file_read - 异步读取文件
 */
ssize_t coco_file_read(int fd, void *buf, size_t count) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !sched->iouring || !coro || fd < 0) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;

    /* 分配请求 */
    iouring_req_t *req = req_alloc(iou);
    if (!req) {
        return COCO_ERROR;
    }

    req->type = IOURING_REQ_READ;
    req->coro = coro;
    req->fd = fd;
    req->buf = buf;
    req->count = count;

    /* 获取 SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iou->ring);
    if (!sqe) {
        req_free(iou, req);
        return COCO_ERROR;
    }

    /* 准备 READ 操作 */
    io_uring_prep_read(sqe, fd, buf, count, 0);
    io_uring_sqe_set_data(sqe, req);

    coro->state = COCO_STATE_WAITING;

    /* 提交并等待 */
    io_uring_submit(&iou->ring);
    coco_yield();

    ssize_t result = req->result;
    req_free(iou, req);

    return result;
}

/**
 * coco_file_write - 异步写入文件
 */
ssize_t coco_file_write(int fd, const void *buf, size_t count) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !sched->iouring || !coro || fd < 0) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;

    /* 分配请求 */
    iouring_req_t *req = req_alloc(iou);
    if (!req) {
        return COCO_ERROR;
    }

    req->type = IOURING_REQ_WRITE;
    req->coro = coro;
    req->fd = fd;
    req->buf = (void *)buf;  /* 去掉 const */
    req->count = count;

    /* 获取 SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iou->ring);
    if (!sqe) {
        req_free(iou, req);
        return COCO_ERROR;
    }

    /* 准备 WRITE 操作 */
    io_uring_prep_write(sqe, fd, buf, count, 0);
    io_uring_sqe_set_data(sqe, req);

    coro->state = COCO_STATE_WAITING;

    /* 提交并等待 */
    io_uring_submit(&iou->ring);
    coco_yield();

    ssize_t result = req->result;
    req_free(iou, req);

    return result;
}

/**
 * coco_file_close - 异步关闭文件
 */
int coco_file_close(int fd) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !sched->iouring || !coro || fd < 0) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;

    /* 分配请求 */
    iouring_req_t *req = req_alloc(iou);
    if (!req) {
        return COCO_ERROR;
    }

    req->type = IOURING_REQ_CLOSE;
    req->coro = coro;
    req->fd = fd;

    /* 获取 SQE */
    struct io_uring_sqe *sqe = io_uring_get_sqe(&iou->ring);
    if (!sqe) {
        req_free(iou, req);
        return COCO_ERROR;
    }

    /* 准备 CLOSE 操作 */
    io_uring_prep_close(sqe, fd);
    io_uring_sqe_set_data(sqe, req);

    coro->state = COCO_STATE_WAITING;

    /* 提交并等待 */
    io_uring_submit(&iou->ring);
    coco_yield();

    int result = (int)req->result;
    req_free(iou, req);

    return result;
}

/* === 批量 I/O API === */

/**
 * coco_iouring_submit_batch - 批量提交 I/O 请求
 */
int coco_iouring_submit_batch(coco_sched_t *sched) {
    if (!sched || !sched->iouring) {
        return COCO_ERROR;
    }

    coco_iouring_t *iou = sched->iouring;
    int ret = io_uring_submit(&iou->ring);

    if (ret > 0) {
        iou->submit_count++;
        iou->syscall_count++;
    }

    return ret;
}

/**
 * coco_iouring_get_stats - 获取 io_uring 统计信息
 */
void coco_iouring_get_stats(coco_sched_t *sched, uint64_t *submit_count, uint64_t *syscall_count) {
    if (!sched || !sched->iouring) {
        *submit_count = 0;
        *syscall_count = 0;
        return;
    }

    coco_iouring_t *iou = sched->iouring;
    *submit_count = iou->submit_count;
    *syscall_count = iou->syscall_count;
}

#else /* !__linux__ */

/* 非 Linux 平台的空实现 */
int coco_poll_init_iouring(coco_sched_t *sched) { return COCO_ERROR; }
void coco_poll_cleanup_iouring(coco_sched_t *sched) { (void)sched; }
int coco_poll_register_iouring(coco_sched_t *sched, int fd, coco_coro_t *coro, short events) {
    (void)sched; (void)fd; (void)coro; (void)events;
    return COCO_ERROR;
}
void coco_poll_unregister_iouring(coco_sched_t *sched, int fd) { (void)sched; (void)fd; }
int coco_poll_wait_iouring(coco_sched_t *sched, int timeout_ms) {
    (void)sched; (void)timeout_ms;
    return 0;
}

#endif /* __linux__ */
