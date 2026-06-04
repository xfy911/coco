/**
 * netpoller_mt.h - Netpoller 多线程模型 (Phase 1, US-008)
 *
 * 专用 netpoller 线程，处理 I/O 事件并分发到对应 P。
 */

#ifndef NETPOLLER_MT_H
#define NETPOLLER_MT_H

#include "../coco_internal.h"
#include <pthread.h>
#include <stdatomic.h>

/* FD 关联的协程信息 */
typedef struct coco_fd_info {
    int fd;
    struct coco_coro *read_coro;   /* 等待读的协程 */
    struct coco_coro *write_coro;  /* 等待写的协程 */
    uint32_t target_p;             /* 目标 P ID */
} coco_fd_info_t;

/* FD 信息表 */
typedef struct coco_fd_info_table {
    coco_fd_info_t **table;
    uint32_t capacity;
    uint32_t max_fd;
} coco_fd_info_table_t;

/* Netpoller 结构 */
typedef struct coco_netpoller {
    /* 线程 */
    pthread_t thread;
    _Atomic bool running;

    /* kqueue/epoll fd */
    int poll_fd;

    /* FD 信息表 */
    coco_fd_info_table_t *fd_table;

    /* 互斥锁 */
    pthread_mutex_t lock;

    /* 条件变量 (用于唤醒 poll) */
    pthread_cond_t wake_cond;
#ifdef __APPLE__
    int wakeup_fd;       /* pipe 写端 (kqueue) */
    int wakeup_read_fd;  /* pipe 读端 (kqueue) */
#else
    int wakeup_fd;       /* eventfd (epoll) */
#endif

    /* 关联的全局调度器 */
    struct coco_global_sched *sched;

    /* 统计 */
    _Atomic uint64_t events_processed;
    _Atomic uint64_t wakeups;
} coco_netpoller_t;

/* API */
coco_netpoller_t *coco_netpoller_create(struct coco_global_sched *sched);
void coco_netpoller_destroy(coco_netpoller_t *np);

/* 启动/停止 */
int coco_netpoller_start(coco_netpoller_t *np);
int coco_netpoller_stop(coco_netpoller_t *np);

/* FD 注册 */
int coco_netpoller_register(coco_netpoller_t *np, int fd, uint32_t events,
                            struct coco_coro *coro, uint32_t target_p);
int coco_netpoller_unregister(coco_netpoller_t *np, int fd, uint32_t events);

/* 唤醒 poll (用于注入新事件) */
int coco_netpoller_wakeup(coco_netpoller_t *np);

/* 统计 */
uint64_t coco_netpoller_events_processed(coco_netpoller_t *np);
uint64_t coco_netpoller_wakeups(coco_netpoller_t *np);

#endif /* NETPOLLER_MT_H */
