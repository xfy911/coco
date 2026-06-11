/**
 * coco_internal.h - 内部头文件
 */

#ifndef COCO_INTERNAL_H
#define COCO_INTERNAL_H

#include "../include/coco.h"
#include "../include/coco_safety.h"
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

/* 上下文结构 (与汇编对应，支持 x86-64 和 ARM64) */
/* ARM64 上下文结构 - 包含浮点寄存器 */
#if defined(__aarch64__) || defined(_M_ARM64)
typedef struct coco_ctx {
    void *sp;           /* offset 0: 栈指针 */
    void *fp;           /* offset 8: 帧指针 (x29) */
    void *lr;           /* offset 16: 链接寄存器 (x30) */
    void *x19;          /* offset 24: callee-saved */
    void *x20;          /* offset 32 */
    void *x21;          /* offset 40 */
    void *x22;          /* offset 48 */
    void *x23;          /* offset 56 */
    void *x24;          /* offset 64 */
    void *x25;          /* offset 72 */
    void *x26;          /* offset 80 */
    void *x27;          /* offset 88 */
    void *x28;          /* offset 96 */
    double d8;          /* offset 104: 浮点 callee-saved (8字节对齐) */
    double d9;          /* offset 112 */
    double d10;         /* offset 120 */
    double d11;         /* offset 128 */
    double d12;         /* offset 136 */
    double d13;         /* offset 144 */
    double d14;         /* offset 152 */
    double d15;         /* offset 160 */
    /* 汇编不保存以下字段，由 C 代码管理 */
    void *stack_base;   /* offset 168: 动态栈增长 (C 管理) */
    void *stack_limit;  /* offset 176: 动态栈增长 (C 管理) */
} coco_ctx_t;
#define COCO_CTX_SIZE 184
#define COCO_CTX_ASM_SIZE 168  /* 汇编保存的大小 */

/* Windows x86-64 上下文结构 - 包含 XMM 寄存器 (16字节对齐) */
#elif defined(_WIN32) && defined(__x86_64__)
typedef struct coco_ctx {
    void *sp;           /* offset 0: 栈指针 */
    void *fp;           /* offset 8: 基址指针 */
    void *rbx;          /* offset 16: callee-saved */
    void *rsi;          /* offset 24: callee-saved (Windows) */
    void *rdi;          /* offset 32: callee-saved (Windows) */
    void *r12;          /* offset 40: callee-saved */
    void *r13;          /* offset 48: callee-saved */
    void *r14;          /* offset 56: callee-saved */
    void *r15;          /* offset 64: callee-saved */
    /* 8 字节填充，确保 XMM 16 字节对齐 */
    uint64_t _pad0;     /* offset 72: 填充 */
    /* XMM 寄存器从 offset 80 开始，16 字节对齐 */
    uint64_t xmm6[2];   /* offset 80: callee-saved (Windows, 16字节) */
    uint64_t xmm7[2];   /* offset 96 */
    uint64_t xmm8[2];   /* offset 112 */
    uint64_t xmm9[2];   /* offset 128 */
    uint64_t xmm10[2];  /* offset 144 */
    uint64_t xmm11[2];  /* offset 160 */
    uint64_t xmm12[2];  /* offset 176 */
    uint64_t xmm13[2];  /* offset 192 */
    uint64_t xmm14[2];  /* offset 208 */
    uint64_t xmm15[2];  /* offset 224 */
    /* 汇编不保存以下字段，由 C 代码管理 */
    void *stack_base;   /* offset 240: 动态栈增长 (C 管理) */
    void *stack_limit;  /* offset 248: 动态栈增长 (C 管理) */
} coco_ctx_t;
#define COCO_CTX_SIZE 256
#define COCO_CTX_ASM_SIZE 240  /* 汇编保存的大小 */

/* Unix x86-64 上下文结构 */
#elif defined(__x86_64__)
typedef struct coco_ctx {
    void *sp;           /* offset 0: 栈指针 */
    void *fp;           /* offset 8: 基址指针 */
    void *rbx;          /* offset 16: callee-saved */
    void *r12;          /* offset 24: callee-saved */
    void *r13;          /* offset 32: callee-saved */
    void *r14;          /* offset 40: callee-saved */
    void *r15;          /* offset 48: callee-saved */
    /* 汇编不保存以下字段，由 C 代码管理 */
    void *stack_base;   /* offset 56: 动态栈增长 (C 管理) */
    void *stack_limit;  /* offset 64: 动态栈增长 (C 管理) */
} coco_ctx_t;
#define COCO_CTX_SIZE 72
#define COCO_CTX_ASM_SIZE 56  /* 汇编保存的大小 */
#endif

/* Channel select types */
typedef struct coco_select_node {
    coco_channel_t *chan;        /* Channel this node is registered on */
    coco_coro_t *coro;           /* Owning coroutine */
    int case_index;              /* Index in the select case array */
    int is_send;                 /* 1=send case, 0=recv case */
    union {
        void *send_val;          /* Value to send (for send cases) */
        void **recv_ptr;         /* Pointer to store received value (for recv cases) */
    };
    int registered;              /* 1 if currently on channel wait queue */
    struct coco_select_node *next; /* Link to next node in same select */
} coco_select_node_t;

#include "core/hot_stack.h"

/* 协程结构 */
struct coco_coro {
    uint64_t id;
    _Atomic coco_state_t state;
    coco_ctx_t ctx;

    void *stack_base;      /* 栈起始地址 */
    void *stack_top;       /* 栈顶地址 */
    size_t stack_size;     /* 栈大小 */
    void *stack_pool;      /* 分配此栈的池指针，释放时使用 */

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

    /* Context 关联 (Phase 2) */
    struct coco_context *context;  /* 关联的 context */

    /* 嵌入式 Channel 等待节点 */
    struct {
        coco_coro_t *next_waiter;  /* 等待队列下一节点 */
        void *value;               /* 传递的值 */
        bool in_use;               /* 是否在等待队列中 */
        bool freed_by_destroy;     /* destroy 已释放标志 */
        void *channel;             /* 等待的 channel（用于取消时清理，void* 支持不同 channel 类型） */
    } wait_node;

    /* O_NONBLOCK 缓存位图 (FD 0-31) */
    uint32_t nonblock_fds_set;

    /* Dynamic stack growth fields (Phase 8) */
    coco_safety_mode_t safety_mode;     /* Safety mode for this coroutine */
    size_t max_stack_size;              /* Maximum allowed stack size */
    bool stack_growable;                /* Whether stack can grow dynamically */
    size_t current_stack_size;          /* Current stack size (updated on growth) */
    bool stack_from_pool;               /* True if current stack was allocated from pool */

    /* Channel select state (NULL when not in select) */
    coco_select_node_t *select_nodes;
    int select_case_count;
    int select_ready_index;           /* -1 = none ready, -2 = timeout, -3 = default */
    coco_timer_t *select_timer;       /* Timer for select timeout (NULL if none) */

    /* Time-slice fairness (Phase 2) */
    uint64_t runtime_start_ns;        /* 当前运行周期开始时间（纳秒） */
    bool time_slice_expired;          /* 时间片已到期标志 */

    /* Hot stack management (Shared Stack) */
    void *stack_backup;
    size_t stack_backup_size;
    size_t stack_used;
    int hot_slot_idx;
    coco_hot_node_t hot_node;
    uint64_t last_run_tick;
    bool is_exclusive;

    /* Coroutine-local storage */
    struct cls_entry *cls_table;
};

/* 时间轮结构（前置声明） */
typedef struct coco_timer_wheel coco_timer_wheel_t;

/* 栈池结构（前置声明） */
typedef struct stack_pool stack_pool_t;

/* 定时器结构 */
struct coco_timer {
    uint64_t expire_time_ms;    /* 过期时间（毫秒） */
    coco_coro_t *coro;          /* 关联协程 */
    void (*callback)(void*);    /* 回调函数 */
    void *arg;                  /* 回调参数 */
    coco_timer_t *next;         /* 链表下一节点 */
    coco_timer_t *prev;         /* 链表上一节点 (O(1) 取消) */
    int level;                  /* 所在层级 (1-4) */
    int slot;                   /* 所在槽位 */
    bool cancelled;             /* 已取消标志 */
};

/* FD 表结构 */
typedef struct fd_table {
    coco_coro_t **table;      /* FD 到协程的映射数组 */
    uint32_t capacity;        /* 当前容量 */
    uint32_t max_fd;          /* 已注册的最大 FD 值 */
} fd_table_t;

/* I/O 后端类型 */
typedef enum {
    COCO_POLL_EPOLL,      /* Linux epoll */
    COCO_POLL_KQUEUE,     /* macOS kqueue */
    COCO_POLL_IOURING,    /* Linux io_uring */
    COCO_POLL_WSAPOLL     /* Windows WSAPoll */
} coco_poll_backend_t;

/* I/O 配置常量 */
#define COCO_EPOLL_MAX_EVENTS  256   /* epoll_wait 最大事件数 */
#define COCO_KQUEUE_MAX_EVENTS 256   /* kevent 最大事件数 */
#define COCO_WSAPOLL_MAX_EVENTS 256  /* WSAPoll 最大事件数 */

/* 强制后端选择标志 */
typedef struct {
    coco_io_backend_t forced_backend;  /* 用户强制选择的后端 */
    bool backend_forced;               /* 是否强制选择 */
} coco_poll_config_t;

/* io_uring 上下文（前置声明） */
typedef struct coco_iouring coco_iouring_t;

/* io_uring 请求（前置声明） */
typedef struct iouring_req iouring_req_t;

/* 批量 I/O 上下文（前置声明） */
typedef struct coco_batch_io coco_batch_io_t;

/* 调度器结构 */
struct coco_sched {
    coco_coro_t *current;      /* 当前运行协程 */

    /* 多优先级运行队列 */
    coco_coro_t *ready_heads[COCO_PRIORITY_COUNT];   /* 各优先级的队列头 */
    coco_coro_t *ready_tails[COCO_PRIORITY_COUNT];   /* 各优先级的队列尾 */
    uint32_t ready_counts[COCO_PRIORITY_COUNT];      /* 各优先级的协程数 */
    uint32_t ready_count;                             /* 总就绪协程数 */
    uint32_t ready_bitmap;                            /* 位图: bit i = 1 表示优先级 i 非空 */
    uint32_t dequeue_count;                           /* 出队计数器（用于周期性老化检查） */

    coco_coro_t **coro_table;  /* 协程池（ID 映射） */
    uint32_t coro_count;
    uint32_t coro_capacity;

    coco_ctx_t main_ctx;       /* 主上下文（调度器返回点） */

    uint64_t next_id;          /* 下一个协程 ID */

    /* 事件循环 */
    int poll_fd;               /* epoll/kqueue/io_uring 实例 */
    coco_poll_backend_t poll_backend;  /* I/O 后端类型 */
    coco_iouring_t *iouring;   /* io_uring 上下文（Linux 5.1+） */
    coco_timer_wheel_t *timer_wheel;  /* 时间轮 */

    /* 栈池 */
    stack_pool_t *stack_pool;  /* 栈池（单线程设计，无需同步） */

    /* FD 表 */
    fd_table_t *fd_table;      /* FD 到协程的映射 */

    /* I/O 后端配置 */
    coco_poll_config_t poll_config;  /* 后端强制选择配置 */
    coco_io_options_t io_options;    /* I/O 配置选项 */
    bool io_options_set;             /* 是否设置了自定义配置 */

    /* 老化配置 */
    uint64_t aging_threshold_ms;  /* 老化阈值（等待多久后提升优先级） */

    /* Time-slice fairness (Phase 2) */
    uint64_t time_slice_ns;       /* 时间片长度（纳秒），默认 10ms */
    bool fairness_enabled;        /* 是否启用公平调度 */

    /* Stack map for dynamic stack growth (Phase 11) */
    struct coco_stack_map *stack_map;  /* Loaded stack map for pointer adjustment */

    /* Hot stack management (Shared Stack) */
    coco_hot_slot_t *hot_slots;
    int hot_slot_count;
    coco_hot_node_t *hot_lru_head;
    coco_hot_node_t *hot_lru_tail;
    int hot_coro_count;
    uint64_t sched_tick;
};

/* 上下文 API (汇编实现) */
void coco_ctx_save(coco_ctx_t *ctx);
void coco_ctx_load(coco_ctx_t *ctx);
void coco_ctx_switch(coco_ctx_t *current, coco_ctx_t *target);

/* 线程局部返回上下文 (ST: &sched->main_ctx, MT: &p->m->ctx) */
extern _Thread_local coco_ctx_t *g_return_ctx;

/* 线程局部调度器和协程 (定义在 coro.c) */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/* 协程入口包装函数 (定义在 coro.c) */
void coro_entry_wrapper(void *arg);

/* Safety mode (定义在 safety.c) */
extern coco_safety_mode_t g_safety_mode;

/* 抢占信号阻塞 API */
int coco_preempt_block_signal(void);
int coco_preempt_unblock_signal(void);

/* 协程上下文检查宏 */
#define ENSURE_IN_CORO_RET(retval) do { \
    if (!g_current_coro) return (retval); \
} while(0)

#define ENSURE_IN_CORO_VOID() do { \
    if (!g_current_coro) return; \
} while(0)

#define ENSURE_IN_CORO() ENSURE_IN_CORO_RET(COCO_ERROR_INVALID)

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

/* 快速时间获取 API (Phase 2) */
uint64_t coco_get_time_fast(void);

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
void coco_poll_set_nonblock(int fd);
void coco_poll_wait_ready(int fd, short events);

/* io_uring API (Linux 5.1+) */
int coco_poll_init_iouring(coco_sched_t *sched);
void coco_poll_cleanup_iouring(coco_sched_t *sched);
int coco_poll_register_iouring(coco_sched_t *sched, int fd, coco_coro_t *coro, short events);
void coco_poll_unregister_iouring(coco_sched_t *sched, int fd);
int coco_poll_wait_iouring(coco_sched_t *sched, int timeout_ms);
int coco_poll_cancel_iouring(coco_sched_t *sched, iouring_req_t *req);
int coco_iouring_submit_batch(coco_sched_t *sched);
void coco_iouring_get_stats(coco_sched_t *sched, uint64_t *submit_count, uint64_t *syscall_count);

/* FD 表 API */
fd_table_t *fd_table_create(uint32_t initial_capacity);
void fd_table_destroy(fd_table_t *ft);
coco_coro_t *fd_table_get(fd_table_t *ft, int fd);
int fd_table_set(fd_table_t *ft, int fd, coco_coro_t *coro);
void fd_table_clear(fd_table_t *ft, int fd);

/* Channel select cleanup (for coco_destroy) */
void coco_select_cleanup(coco_coro_t *coro);

/* Windows APC preemption alertable sleep */
#ifdef _WIN32
void coco_preempt_sleep_ex(uint64_t timeout_ms);
#endif

#endif /* COCO_INTERNAL_H */
