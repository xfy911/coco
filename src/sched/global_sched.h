/**
 * global_sched.h - 全局调度器框架 (Phase 1)
 *
 * M:N 多线程调度架构，参考 Go runtime 设计。
 */

/*
 * 锁获取顺序 (从高层级到低层级):
 *
 * 层级 1: global_runq_lock (全局运行队列)
 * 层级 2: idle_lock (空闲等待，仅 worker_loop 使用)
 * 层级 3: channel.wait_queue_lock (Channel 等待队列操作)
 * 层级 4: local_runq_lock (Per-P 本地运行队列)
 *
 * 规则:
 * - 永远不要在持有低层级锁时获取高层级锁
 * - Channel 唤醒协程时: 先释放 wait_queue_lock，再获取 local_runq_lock
 * - 空闲等待时: 持 idle_lock 检查运行队列，不获取其他锁
 *
 * 特殊情况:
 * - destroy 时可持 wait_queue_lock 排空队列，不唤醒协程
 */

#ifndef GLOBAL_SCHED_H
#define GLOBAL_SCHED_H

#include "../coco_internal.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

/* 前向声明 */
struct coco_processor;
struct coco_machine;
struct coco_timer_wheel;
struct coco_sched;
struct coco_netpoller;

/* 全局调度器 */
typedef struct coco_global_sched {
    /* 处理器数组 */
    struct coco_processor **processors;
    uint32_t processor_count;
    uint32_t processor_mask;

    /* 全局运行队列 (互斥锁保护) */
    struct coco_coro *global_runq_head;
    struct coco_coro *global_runq_tail;
    uint64_t global_runq_size;
    pthread_mutex_t global_runq_lock;

    /* 空闲 P 列表 */
    struct coco_processor *idle_processors;
    pthread_mutex_t idle_lock;

    /* 空闲 M 列表 */
    struct coco_machine *idle_machines;
    pthread_cond_t idle_cond;

    /* 主调度器 (用于定时器唤醒) */
    struct coco_sched *main_sched;

    /* Netpoller (专用 I/O 轮询线程) */
    struct coco_netpoller *netpoller;

    /* 统计信息 */
    _Atomic uint64_t total_coroutines;
    _Atomic uint64_t active_coroutines;
    _Atomic uint64_t next_coro_id;

    /* 状态 */
    _Atomic bool running;
} coco_global_sched_t;

/* 处理器 (P) - 逻辑处理器 */
typedef struct coco_processor {
    uint32_t id;

    /* 本地运行队列 (锁保护) */
    struct coco_coro *local_runq_head;
    struct coco_coro *local_runq_tail;
    uint32_t local_runq_size;
    pthread_mutex_t local_runq_lock;

    /* 当前运行的协程 */
    _Atomic(struct coco_coro*) curcoro;

    /* 绑定的 M */
    struct coco_machine *m;

    /* 栈池 (Per-P，避免竞争) */
    void *stack_pool;  /* stack_pool_multi_t* */

    /* Per-P 时间轮 */
    struct coco_timer_wheel *timer_wheel;

    /* 反向引用全局调度器 */
    struct coco_global_sched *global_sched;

    /* 状态 */
    _Atomic enum {
        P_IDLE,
        P_RUNNING,
        P_SYSCALL
    } status;

    struct coco_processor *next;
} coco_processor_t;

/* 机器 (M) - OS 线程 */
typedef struct coco_machine {
    uint32_t id;
    pthread_t thread;

    /* 绑定的 P */
    struct coco_processor *p;

    coco_ctx_t ctx;  /* Worker thread context for coroutine switching */

    /* 状态 */
    _Atomic enum {
        M_IDLE,
        M_RUNNING,
        M_BLOCKED
    } status;

    struct coco_machine *next;
} coco_machine_t;

/* API */
int coco_global_init(uint32_t num_procs);
void coco_global_destroy(void);
coco_global_sched_t *coco_global_get(void);

/* P 操作 */
coco_processor_t *coco_processor_create(uint32_t id);
void coco_processor_destroy(coco_processor_t *p);

/* M 操作 */
coco_machine_t *coco_machine_create(uint32_t id);
void coco_machine_destroy(coco_machine_t *m);

/* 全局队列操作 */
int coco_global_runq_put(struct coco_coro *g);
struct coco_coro *coco_global_runq_get(void);
uint64_t coco_global_runq_size(void);

/* 查询 */
uint32_t coco_processor_count(void);
coco_processor_t *coco_processor_get(uint32_t id);

/* Multi-threaded scheduler API */
int coco_global_sched_start(uint32_t num_workers);
int coco_global_sched_wait(void);
int coco_global_sched_stop(void);

#endif /* GLOBAL_SCHED_H */