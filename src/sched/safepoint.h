/**
 * safepoint.h - 安全点机制 (Phase 3, US-011)
 *
 * 安全点定义:
 * - 无锁持有: 协程不在持有任何锁
 * - 无部分更新: 数据结构处于一致状态
 * - 栈一致性: 栈帧完整，可以安全切换
 *
 * 协作式抢占的安全点定义和实现。
 */

#ifndef SAFEPOINT_H
#define SAFEPOINT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>

/* 项目级断言宏 */
#ifdef COCO_DEBUG
#define COCO_ASSERT(cond) assert(cond)
#else
#define COCO_ASSERT(cond) ((void)0)
#endif

/* 安全点状态 */
typedef enum safepoint_state {
    SAFEPOINT_STATE_SAFE,       /* 可以安全抢占 */
    SAFEPOINT_STATE_LOCKED,     /* 持有锁，不能抢占 */
    SAFEPOINT_STATE_UPDATING,   /* 正在更新数据，不能抢占 */
    SAFEPOINT_STATE_BLOCKING    /* 在阻塞操作中，不能抢占 */
} safepoint_state_t;

/* 安全点上下文 */
typedef struct safepoint_ctx {
    _Atomic uint32_t preempt_request;   /* 抢占请求标志 */
    _Atomic uint32_t locks_held;         /* 持有的锁数量 (调试用) */
    _Atomic safepoint_state_t state;     /* 当前状态 */
    uint64_t last_yield_time;            /* 上次让出时间 */
    uint64_t yield_count;                /* 让出次数 */
    uint64_t safepoint_count;            /* 安全点检查次数 */
    uint64_t preempt_success_count;      /* 成功抢占次数 */

    /* 性能追踪 (调试模式) */
    uint64_t total_safepoint_ns;         /* 安全点总耗时 */
    uint64_t max_safepoint_ns;           /* 单次最大耗时 */
} safepoint_ctx_t;

/* 创建安全点上下文 */
safepoint_ctx_t *safepoint_ctx_create(void);
void safepoint_ctx_destroy(safepoint_ctx_t *ctx);

/* 请求抢占 */
void safepoint_request_preempt(safepoint_ctx_t *ctx);

/* 检查是否需要抢占 */
bool safepoint_check_preempt(safepoint_ctx_t *ctx);

/* 清除抢占请求 */
void safepoint_clear_preempt(safepoint_ctx_t *ctx);

/* 状态管理 */
void safepoint_set_state(safepoint_ctx_t *ctx, safepoint_state_t state);
safepoint_state_t safepoint_get_state(safepoint_ctx_t *ctx);
bool safepoint_is_safe(safepoint_ctx_t *ctx);

/* 锁追踪 (调试模式) */
void safepoint_lock_enter(safepoint_ctx_t *ctx);
void safepoint_lock_exit(safepoint_ctx_t *ctx);
uint32_t safepoint_locks_held(safepoint_ctx_t *ctx);

/* 统计 */
uint64_t safepoint_get_yield_count(safepoint_ctx_t *ctx);
uint64_t safepoint_get_safepoint_count(safepoint_ctx_t *ctx);
uint64_t safepoint_get_preempt_success_count(safepoint_ctx_t *ctx);

/* 性能统计 (调试模式) */
double safepoint_get_avg_ns(safepoint_ctx_t *ctx);
uint64_t safepoint_get_max_ns(safepoint_ctx_t *ctx);

/* 安全点宏 - 带性能追踪 */
#ifdef COCO_DEBUG
#define COCO_SAFEPOINT(ctx) do { \
    if (ctx) { \
        uint64_t start_ns = safepoint_get_time_ns(); \
        COCO_ASSERT(atomic_load(&(ctx)->locks_held) == 0 && "Safe point with locks held"); \
        COCO_ASSERT(atomic_load(&(ctx)->state) == SAFEPOINT_STATE_SAFE && "Safe point in unsafe state"); \
        (ctx)->safepoint_count++; \
        if (atomic_load_explicit(&(ctx)->preempt_request, memory_order_relaxed)) { \
            atomic_store(&(ctx)->preempt_request, 0); \
            (ctx)->preempt_success_count++; \
            safepoint_do_yield(ctx); \
        } \
        uint64_t elapsed_ns = safepoint_get_time_ns() - start_ns; \
        (ctx)->total_safepoint_ns += elapsed_ns; \
        if (elapsed_ns > (ctx)->max_safepoint_ns) { \
            (ctx)->max_safepoint_ns = elapsed_ns; \
        } \
    } \
} while (0)
#else
#define COCO_SAFEPOINT(ctx) do { \
    if (ctx && atomic_load_explicit(&(ctx)->preempt_request, memory_order_relaxed)) { \
        atomic_store(&(ctx)->preempt_request, 0); \
        safepoint_do_yield(ctx); \
    } \
} while (0)
#endif

/* 锁持有标记宏 (调试模式) */
#ifdef COCO_DEBUG
#define COCO_LOCK_ENTER(ctx) safepoint_lock_enter(ctx)
#define COCO_LOCK_EXIT(ctx) safepoint_lock_exit(ctx)
#else
#define COCO_LOCK_ENTER(ctx) ((void)0)
#define COCO_LOCK_EXIT(ctx) ((void)0)
#endif

/* 更新状态标记宏 */
#ifdef COCO_DEBUG
#define COCO_UPDATE_BEGIN(ctx) safepoint_set_state(ctx, SAFEPOINT_STATE_UPDATING)
#define COCO_UPDATE_END(ctx) safepoint_set_state(ctx, SAFEPOINT_STATE_SAFE)
#else
#define COCO_UPDATE_BEGIN(ctx) ((void)0)
#define COCO_UPDATE_END(ctx) ((void)0)
#endif

/* 内部函数 */
void safepoint_do_yield(safepoint_ctx_t *ctx);
uint64_t safepoint_get_time_ns(void);

#endif /* SAFEPOINT_H */