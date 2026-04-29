/**
 * safepoint.c - 安全点机制实现 (Phase 3, US-011)
 *
 * 安全点定义:
 * - 无锁持有: 协程不在持有任何锁
 * - 无部分更新: 数据结构处于一致状态
 * - 栈一致性: 栈帧完整，可以安全切换
 */

#include "safepoint.h"
#include <stdlib.h>
#include <time.h>

/* 获取当前时间（纳秒） */
uint64_t safepoint_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* 创建安全点上下文 */
safepoint_ctx_t *safepoint_ctx_create(void) {
    safepoint_ctx_t *ctx = calloc(1, sizeof(safepoint_ctx_t));
    if (!ctx) {
        return NULL;
    }

    atomic_init(&ctx->preempt_request, 0);
    atomic_init(&ctx->locks_held, 0);
    atomic_init(&ctx->state, SAFEPOINT_STATE_SAFE);
    ctx->last_yield_time = 0;
    ctx->yield_count = 0;
    ctx->safepoint_count = 0;
    ctx->preempt_success_count = 0;
    ctx->total_safepoint_ns = 0;
    ctx->max_safepoint_ns = 0;

    return ctx;
}

/* 销毁安全点上下文 */
void safepoint_ctx_destroy(safepoint_ctx_t *ctx) {
    if (ctx) {
        free(ctx);
    }
}

/* 请求抢占 */
void safepoint_request_preempt(safepoint_ctx_t *ctx) {
    if (ctx) {
        atomic_store_explicit(&ctx->preempt_request, 1, memory_order_release);
    }
}

/* 检查是否需要抢占 */
bool safepoint_check_preempt(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return false;
    }
    return atomic_load_explicit(&ctx->preempt_request, memory_order_acquire) != 0;
}

/* 清除抢占请求 */
void safepoint_clear_preempt(safepoint_ctx_t *ctx) {
    if (ctx) {
        atomic_store_explicit(&ctx->preempt_request, 0, memory_order_release);
    }
}

/* 设置状态 */
void safepoint_set_state(safepoint_ctx_t *ctx, safepoint_state_t state) {
    if (ctx) {
        atomic_store(&ctx->state, state);
    }
}

/* 获取状态 */
safepoint_state_t safepoint_get_state(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return SAFEPOINT_STATE_SAFE;
    }
    return atomic_load(&ctx->state);
}

/* 检查是否安全 */
bool safepoint_is_safe(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return true;
    }
    return atomic_load(&ctx->state) == SAFEPOINT_STATE_SAFE &&
           atomic_load(&ctx->locks_held) == 0;
}

/* 锁追踪 (调试模式) */
void safepoint_lock_enter(safepoint_ctx_t *ctx) {
    if (ctx) {
        atomic_fetch_add(&ctx->locks_held, 1);
    }
}

void safepoint_lock_exit(safepoint_ctx_t *ctx) {
    if (ctx) {
        atomic_fetch_sub(&ctx->locks_held, 1);
    }
}

uint32_t safepoint_locks_held(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return atomic_load(&ctx->locks_held);
}

/* 统计 */
uint64_t safepoint_get_yield_count(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->yield_count;
}

uint64_t safepoint_get_safepoint_count(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->safepoint_count;
}

uint64_t safepoint_get_preempt_success_count(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->preempt_success_count;
}

/* 性能统计 */
double safepoint_get_avg_ns(safepoint_ctx_t *ctx) {
    if (!ctx || ctx->safepoint_count == 0) {
        return 0.0;
    }
    return (double)ctx->total_safepoint_ns / ctx->safepoint_count;
}

uint64_t safepoint_get_max_ns(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->max_safepoint_ns;
}

/* 内部让出函数 */
void safepoint_do_yield(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return;
    }

    ctx->last_yield_time = safepoint_get_time_ns();
    ctx->yield_count++;
}
