/**
 * netpoller_mt.c - Netpoller 多线程实现 (Phase 1, US-008)
 *
 * 专用 netpoller 线程实现，支持 macOS (kqueue) 和 Linux (epoll)。
 */

#include "netpoller_mt.h"
#include "../coco_internal.h"
#include "../sched/sched.h"
#include "../sched/global_sched.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __APPLE__
#include <sys/event.h>
#define USE_KQUEUE 1
#else
#include <sys/epoll.h>
#include <sys/eventfd.h>
#define USE_EPOLL 1
#endif

/* 外部全局变量 */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/**
 * 创建 FD 信息表
 */
static coco_fd_info_table_t *fd_info_table_create(uint32_t initial_capacity) {
    coco_fd_info_table_t *ft = calloc(1, sizeof(coco_fd_info_table_t));
    if (!ft) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = 1024;
    }

    ft->table = calloc(initial_capacity, sizeof(coco_fd_info_t*));
    if (!ft->table) {
        free(ft);
        return NULL;
    }

    ft->capacity = initial_capacity;
    ft->max_fd = 0;
    return ft;
}

/**
 * 销毁 FD 信息表
 */
static void fd_info_table_destroy(coco_fd_info_table_t *ft) {
    if (!ft) {
        return;
    }

    /* 释放所有 fd_info */
    for (uint32_t i = 0; i <= ft->max_fd; i++) {
        if (ft->table[i]) {
            free(ft->table[i]);
        }
    }

    free(ft->table);
    free(ft);
}

/**
 * 获取指定 FD 的信息
 */
static coco_fd_info_t *fd_info_table_get(coco_fd_info_table_t *ft, int fd) {
    if (!ft || fd < 0 || (uint32_t)fd >= ft->capacity) {
        return NULL;
    }
    return ft->table[fd];
}

/**
 * 设置指定 FD 的信息
 */
static int fd_info_table_set(coco_fd_info_table_t *ft, int fd, coco_fd_info_t *info) {
    if (!ft || fd < 0) {
        return COCO_ERROR;
    }

    /* 扩容 */
    if ((uint32_t)fd >= ft->capacity) {
        uint32_t new_capacity = ft->capacity;
        while (new_capacity <= (uint32_t)fd) {
            new_capacity *= 2;
        }

        coco_fd_info_t **new_table = realloc(ft->table, new_capacity * sizeof(coco_fd_info_t*));
        if (!new_table) {
            return COCO_ERROR_NOMEM;
        }

        memset(new_table + ft->capacity, 0, (new_capacity - ft->capacity) * sizeof(coco_fd_info_t*));
        ft->table = new_table;
        ft->capacity = new_capacity;
    }

    ft->table[fd] = info;
    if ((uint32_t)fd > ft->max_fd) {
        ft->max_fd = fd;
    }

    return COCO_OK;
}

/**
 * Netpoller 线程主循环
 */
static void *netpoller_thread(void *arg) {
    coco_netpoller_t *np = (coco_netpoller_t *)arg;

    while (atomic_load(&np->running)) {
#ifdef USE_KQUEUE
        struct kevent events[64];
        int n = kevent(np->poll_fd, NULL, 0, events, 64, NULL);
#else
        struct epoll_event events[64];
        int n = epoll_wait(np->poll_fd, events, 64, -1);
#endif

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < n; i++) {
#ifdef USE_KQUEUE
            int fd = (int)events[i].ident;
            short filter = events[i].filter;
            void *udata = events[i].udata;

            /* 检查是否是唤醒事件 */
            if (fd == np->wakeup_read_fd) {
                /* 消费唤醒数据 */
                char buf[8];
                while (read(np->wakeup_read_fd, buf, sizeof(buf)) > 0) {}
                atomic_fetch_add(&np->wakeups, 1);
                continue;
            }

            coco_fd_info_t *info = (coco_fd_info_t *)udata;
            if (!info) {
                continue;
            }

            /* 加锁保护协程信息访问 (修复数据竞争) */
            pthread_mutex_lock(&np->lock);

            coco_coro_t *read_coro = info->read_coro;
            coco_coro_t *write_coro = info->write_coro;

            /* 分发事件到目标 P */
            if (filter == EVFILT_READ && read_coro) {
                info->read_coro = NULL;
                pthread_mutex_unlock(&np->lock);
                schedule_ready(read_coro);
            } else if (filter == EVFILT_WRITE && write_coro) {
                info->write_coro = NULL;
                pthread_mutex_unlock(&np->lock);
                schedule_ready(write_coro);
            } else {
                pthread_mutex_unlock(&np->lock);
            }
#else
            struct epoll_event *ev = &events[i];
            int fd = ev->data.fd;
            uint32_t events_mask = ev->events;

            /* 检查是否是唤醒事件 */
            if (fd == np->wakeup_fd) {
                uint64_t val;
                read(np->wakeup_fd, &val, sizeof(val));
                atomic_fetch_add(&np->wakeups, 1);
                continue;
            }

            coco_fd_info_t *info = (coco_fd_info_t *)ev->data.ptr;
            if (!info) {
                continue;
            }

            /* 加锁保护协程信息访问 (修复数据竞争) */
            pthread_mutex_lock(&np->lock);

            coco_coro_t *read_coro = info->read_coro;
            coco_coro_t *write_coro = info->write_coro;

            /* 分发事件 */
            if ((events_mask & EPOLLIN) && read_coro) {
                info->read_coro = NULL;
                pthread_mutex_unlock(&np->lock);
                schedule_ready(read_coro);
            } else if ((events_mask & EPOLLOUT) && write_coro) {
                info->write_coro = NULL;
                pthread_mutex_unlock(&np->lock);
                schedule_ready(write_coro);
            } else {
                pthread_mutex_unlock(&np->lock);
            }
#endif

            atomic_fetch_add(&np->events_processed, 1);
        }
    }

    return NULL;
}

/**
 * coco_netpoller_create - 创建 netpoller
 */
coco_netpoller_t *coco_netpoller_create(coco_global_sched_t *sched) {
    coco_netpoller_t *np = calloc(1, sizeof(coco_netpoller_t));
    if (!np) {
        return NULL;
    }

    np->sched = sched;
    atomic_store(&np->running, false);
    atomic_store(&np->events_processed, 0);
    atomic_store(&np->wakeups, 0);

    /* 创建唤醒机制 (Linux: eventfd, macOS: pipe) */
#ifdef USE_KQUEUE
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        free(np);
        return NULL;
    }
    np->wakeup_fd = pipefd[1];
    np->wakeup_read_fd = pipefd[0];

    /* 设置非阻塞 */
    fcntl(np->wakeup_read_fd, F_SETFL, O_NONBLOCK);
    fcntl(np->wakeup_fd, F_SETFL, O_NONBLOCK);
#else
    np->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (np->wakeup_fd < 0) {
        free(np);
        return NULL;
    }
#endif

    /* 创建 poll fd */
#ifdef USE_KQUEUE
    np->poll_fd = kqueue();
#else
    np->poll_fd = epoll_create1(0);
#endif

    if (np->poll_fd < 0) {
#ifdef USE_KQUEUE
        close(np->wakeup_read_fd);
#endif
        close(np->wakeup_fd);
        free(np);
        return NULL;
    }

    /* 注册唤醒 fd */
#ifdef USE_KQUEUE
    struct kevent kev;
    EV_SET(&kev, np->wakeup_read_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(np->poll_fd, &kev, 1, NULL, 0, NULL);
#else
    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.fd = np->wakeup_fd
    };
    epoll_ctl(np->poll_fd, EPOLL_CTL_ADD, np->wakeup_fd, &ev);
#endif

    /* 创建 FD 信息表 */
    np->fd_table = fd_info_table_create(1024);
    if (!np->fd_table) {
        close(np->poll_fd);
#ifdef USE_KQUEUE
        close(np->wakeup_read_fd);
#endif
        close(np->wakeup_fd);
        free(np);
        return NULL;
    }

    /* 初始化锁和条件变量 */
    if (pthread_mutex_init(&np->lock, NULL) != 0) {
        fd_info_table_destroy(np->fd_table);
        close(np->poll_fd);
#ifdef USE_KQUEUE
        close(np->wakeup_read_fd);
#endif
        close(np->wakeup_fd);
        free(np);
        return NULL;
    }

    if (pthread_cond_init(&np->wake_cond, NULL) != 0) {
        pthread_mutex_destroy(&np->lock);
        fd_info_table_destroy(np->fd_table);
        close(np->poll_fd);
#ifdef USE_KQUEUE
        close(np->wakeup_read_fd);
#endif
        close(np->wakeup_fd);
        free(np);
        return NULL;
    }

    return np;
}

/**
 * coco_netpoller_destroy - 销毁 netpoller
 */
void coco_netpoller_destroy(coco_netpoller_t *np) {
    if (!np) {
        return;
    }

    /* 停止线程 */
    if (atomic_load(&np->running)) {
        coco_netpoller_stop(np);
    }

    pthread_cond_destroy(&np->wake_cond);
    pthread_mutex_destroy(&np->lock);

    if (np->fd_table) {
        fd_info_table_destroy(np->fd_table);
    }

    if (np->poll_fd >= 0) {
        close(np->poll_fd);
    }

    close(np->wakeup_fd);
#ifdef USE_KQUEUE
    close(np->wakeup_read_fd);
#endif
    free(np);
}

/**
 * coco_netpoller_start - 启动 netpoller 线程
 */
int coco_netpoller_start(coco_netpoller_t *np) {
    if (!np) {
        return COCO_ERROR;
    }

    if (atomic_load(&np->running)) {
        return COCO_OK;  /* 已在运行 */
    }

    atomic_store(&np->running, true);

    if (pthread_create(&np->thread, NULL, netpoller_thread, np) != 0) {
        atomic_store(&np->running, false);
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * coco_netpoller_stop - 停止 netpoller 线程
 */
int coco_netpoller_stop(coco_netpoller_t *np) {
    if (!np) {
        return COCO_ERROR;
    }

    if (!atomic_load(&np->running)) {
        return COCO_OK;  /* 已停止 */
    }

    atomic_store(&np->running, false);

    /* 唤醒 poll 使其退出 */
    coco_netpoller_wakeup(np);

    /* 等待线程结束 */
    pthread_join(np->thread, NULL);

    return COCO_OK;
}

/**
 * coco_netpoller_register - 注册 FD
 */
int coco_netpoller_register(coco_netpoller_t *np, int fd, uint32_t events,
                            coco_coro_t *coro, uint32_t target_p) {
    if (!np || fd < 0) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&np->lock);

    /* 获取或创建 FD 信息 */
    coco_fd_info_t *info = fd_info_table_get(np->fd_table, fd);
    if (!info) {
        info = calloc(1, sizeof(coco_fd_info_t));
        if (!info) {
            pthread_mutex_unlock(&np->lock);
            return COCO_ERROR_NOMEM;
        }
        info->fd = fd;
        info->target_p = target_p;
        fd_info_table_set(np->fd_table, fd, info);
    }

    /* 更新协程信息 */
    if (events & 0x01) {  /* READ */
        info->read_coro = coro;
    }
    if (events & 0x02) {  /* WRITE */
        info->write_coro = coro;
    }

    /* 注册到 poll */
#ifdef USE_KQUEUE
    struct kevent kev[2];
    int n = 0;

    if (events & 0x01) {
        EV_SET(&kev[n], fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, info);
        n++;
    }
    if (events & 0x02) {
        EV_SET(&kev[n], fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, info);
        n++;
    }

    if (n > 0) {
        kevent(np->poll_fd, kev, n, NULL, 0, NULL);
    }
#else
    struct epoll_event ev = {
        .events = 0,
        .data.ptr = info
    };

    if (events & 0x01) {
        ev.events |= EPOLLIN | EPOLLONESHOT;
    }
    if (events & 0x02) {
        ev.events |= EPOLLOUT | EPOLLONESHOT;
    }

    /* 首次注册使用 ADD，后续使用 MOD */
    if (epoll_ctl(np->poll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        if (errno == EEXIST) {
            if (epoll_ctl(np->poll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
                pthread_mutex_unlock(&np->lock);
                return COCO_ERROR;
            }
        } else {
            pthread_mutex_unlock(&np->lock);
            return COCO_ERROR;
        }
    }
#endif

    pthread_mutex_unlock(&np->lock);
    return COCO_OK;
}

/**
 * coco_netpoller_unregister - 注销 FD
 */
int coco_netpoller_unregister(coco_netpoller_t *np, int fd, uint32_t events) {
    if (!np || fd < 0) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&np->lock);

    coco_fd_info_t *info = fd_info_table_get(np->fd_table, fd);
    if (!info) {
        pthread_mutex_unlock(&np->lock);
        return COCO_OK;
    }

    /* 清除协程信息 */
    if (events & 0x01) {
        info->read_coro = NULL;
    }
    if (events & 0x02) {
        info->write_coro = NULL;
    }

    /* 从 poll 移除 */
#ifdef USE_KQUEUE
    struct kevent kev[2];
    int n = 0;

    if (events & 0x01) {
        EV_SET(&kev[n], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        n++;
    }
    if (events & 0x02) {
        EV_SET(&kev[n], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        n++;
    }

    if (n > 0) {
        kevent(np->poll_fd, kev, n, NULL, 0, NULL);
    }
#else
    if (!info->read_coro && !info->write_coro) {
        epoll_ctl(np->poll_fd, EPOLL_CTL_DEL, fd, NULL);
    }
#endif

    /* 如果没有协程等待，释放 info */
    if (!info->read_coro && !info->write_coro) {
        fd_info_table_set(np->fd_table, fd, NULL);
        free(info);
    }

    pthread_mutex_unlock(&np->lock);
    return COCO_OK;
}

/**
 * coco_netpoller_wakeup - 唤醒 poll
 */
int coco_netpoller_wakeup(coco_netpoller_t *np) {
    if (!np) {
        return COCO_ERROR;
    }

    /* 写入唤醒数据 */
#ifdef USE_EPOLL
    uint64_t val = 1;
    ssize_t n = write(np->wakeup_fd, &val, sizeof(val));
#else
    char buf = 1;
    ssize_t n = write(np->wakeup_fd, &buf, 1);
#endif
    if (n < 0 && errno != EAGAIN) {
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * coco_netpoller_events_processed - 获取已处理事件数
 */
uint64_t coco_netpoller_events_processed(coco_netpoller_t *np) {
    if (!np) {
        return 0;
    }
    return atomic_load(&np->events_processed);
}

/**
 * coco_netpoller_wakeups - 获取唤醒次数
 */
uint64_t coco_netpoller_wakeups(coco_netpoller_t *np) {
    if (!np) {
        return 0;
    }
    return atomic_load(&np->wakeups);
}
