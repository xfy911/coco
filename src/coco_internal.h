/**
 * coco_internal.h - 内部头文件
 */

#ifndef COCO_INTERNAL_H
#define COCO_INTERNAL_H

#include "../include/coco.h"
#include <stdint.h>
#include <stddef.h>

/* 上下文结构 (与汇编对应，支持 x86-64 和 ARM64) */
typedef struct coco_ctx {
    void *sp;       /* offset 0: 栈指针 */
    void *fp;       /* offset 8: 基址指针 (ARM64: x29, x86-64: rbp) */
    void *lr;       /* offset 16: 链接寄存器 (ARM64: x30) */
    void *x19;      /* offset 24: callee-saved */
    void *x20;      /* offset 32 */
    void *x21;      /* offset 40 */
    void *x22;      /* offset 48 */
    void *x23;      /* offset 56 */
    void *x24;      /* offset 64 */
    void *x25;      /* offset 72 */
    void *x26;      /* offset 80 */
    void *x27;      /* offset 88 */
    void *x28;      /* offset 96 */
} coco_ctx_t;

/* 协程结构 */
struct coco_coro {
    uint64_t id;
    coco_state_t state;
    coco_ctx_t ctx;

    void *stack_base;      /* 栈起始地址 */
    void *stack_top;       /* 栈顶地址 */
    size_t stack_size;     /* 栈大小 */

    void (*entry)(void*);  /* 入口函数 */
    void *arg;             /* 入口参数 */
    void *result;          /* 返回值 */

    struct coco_coro *next;     /* 调度链表节点 */
    struct coco_coro *prev;

    int wait_fd;           /* 等待的 fd（-1 表示无） */
    uint64_t wake_time;    /* 定时唤醒时间（ns） */

    size_t stack_high_water_mark;  /* 栈使用峰值（最低栈指针地址） */

    int cancelled;                 /* 取消标志 */

    coco_priority_t priority;      /* 协程优先级 */
    uint64_t ready_timestamp;      /* 进入就绪队列的时间（用于老化） */

    coco_error_cb error_cb;
};

/* 时间轮结构（前置声明） */
typedef struct coco_timer_wheel coco_timer_wheel_t;

/* 栈池结构（前置声明） */
typedef struct stack_pool stack_pool_t;

/* FD 表结构 */
typedef struct fd_table {
    coco_coro_t **table;      /* FD 到协程的映射数组 */
    uint32_t capacity;        /* 当前容量 */
    uint32_t max_fd;          /* 已注册的最大 FD 值 */
} fd_table_t;

/* 调度器结构 */
struct coco_sched {
    coco_coro_t *current;      /* 当前运行协程 */

    /* 多优先级运行队列 */
    coco_coro_t *ready_heads[COCO_PRIORITY_COUNT];   /* 各优先级的队列头 */
    coco_coro_t *ready_tails[COCO_PRIORITY_COUNT];   /* 各优先级的队列尾 */
    uint32_t ready_counts[COCO_PRIORITY_COUNT];      /* 各优先级的协程数 */
    uint32_t ready_count;                             /* 总就绪协程数 */

    coco_coro_t **coro_table;  /* 协程池（ID 映射） */
    uint32_t coro_count;
    uint32_t coro_capacity;

    coco_ctx_t main_ctx;       /* 主上下文（调度器返回点） */

    uint64_t next_id;          /* 下一个协程 ID */

    /* 事件循环 */
    int poll_fd;               /* epoll/kqueue 实例 */
    coco_timer_wheel_t *timer_wheel;  /* 时间轮 */

    /* 栈池 */
    stack_pool_t *stack_pool;  /* 栈池（单线程设计，无需同步） */

    /* FD 表 */
    fd_table_t *fd_table;      /* FD 到协程的映射 */

    /* 老化配置 */
    uint64_t aging_threshold_ms;  /* 老化阈值（等待多久后提升优先级） */
};

/* 上下文 API (汇编实现) */
void coco_ctx_save(coco_ctx_t *ctx);
void coco_ctx_load(coco_ctx_t *ctx);
void coco_ctx_switch(coco_ctx_t *current, coco_ctx_t *target);

/* 上下文初始化 (C 实现) */
void coco_ctx_init(coco_ctx_t *ctx, void *stack_top, void (*entry)(void*), void *arg);

/* 栈管理 (C 实现) */
void *coco_stack_alloc(size_t size);
void coco_stack_free(void *stack, size_t size);

/* 时间轮 API */
coco_timer_wheel_t *coco_timer_wheel_create(void);
void coco_timer_wheel_destroy(coco_timer_wheel_t *tw);
coco_timer_t *coco_timer_add(coco_timer_wheel_t *tw, uint64_t delay_ms, coco_coro_t *coro);
void coco_timer_tick(coco_timer_wheel_t *tw, coco_sched_t *sched);
uint64_t coco_timer_wheel_next_expire(coco_timer_wheel_t *tw);
uint64_t get_current_time_ms_internal(void);

/* 内部调度辅助函数 */
void enqueue_ready(coco_sched_t *sched, coco_coro_t *coro);

/* 信号处理 API */
int coco_signal_init(coco_sched_t *sched);
void coco_signal_cleanup(void);
int coco_set_overflow_checkpoint(void);

/* I/O 多路复用 API */
int coco_poll_init(coco_sched_t *sched);
void coco_poll_cleanup(coco_sched_t *sched);
int coco_poll_register(coco_sched_t *sched, int fd, coco_coro_t *coro, short events);
void coco_poll_unregister(coco_sched_t *sched, int fd);
int coco_poll_wait(coco_sched_t *sched, int timeout_ms);

/* FD 表 API */
fd_table_t *fd_table_create(uint32_t initial_capacity);
void fd_table_destroy(fd_table_t *ft);
coco_coro_t *fd_table_get(fd_table_t *ft, int fd);
int fd_table_set(fd_table_t *ft, int fd, coco_coro_t *coro);
void fd_table_clear(fd_table_t *ft, int fd);

#endif /* COCO_INTERNAL_H */