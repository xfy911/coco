/**
 * safepoint.h - 安全点机制 (Phase 0 验证)
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

/* 安全点上下文 */
typedef struct safepoint_ctx {
    _Atomic uint32_t preempt_request;   /* 抢占请求标志 */
    _Atomic uint32_t locks_held;         /* 持有的锁数量 (调试用) */
    uint64_t last_yield_time;            /* 上次让出时间 */
    uint64_t yield_count;                /* 让出次数 */
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

/* 锁追踪 (调试模式) */
void safepoint_lock_enter(safepoint_ctx_t *ctx);
void safepoint_lock_exit(safepoint_ctx_t *ctx);
uint32_t safepoint_locks_held(safepoint_ctx_t *ctx);

/* 统计 */
uint64_t safepoint_get_yield_count(safepoint_ctx_t *ctx);

/* 安全点宏 */
#ifdef COCO_DEBUG
#define COCO_SAFEPOINT(ctx) do { \
    if (ctx) { \
        COCO_ASSERT(atomic_load(&(ctx)->locks_held) == 0 && "Safe point with locks held"); \
        if (atomic_load_explicit(&(ctx)->preempt_request, memory_order_relaxed)) { \
            atomic_store(&(ctx)->preempt_request, 0); \
            safepoint_do_yield(ctx); \
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

/* 内部让出函数 */
void safepoint_do_yield(safepoint_ctx_t *ctx);

#endif /* SAFEPOINT_H */