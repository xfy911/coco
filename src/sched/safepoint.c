/**
 * safepoint.c - 安全点机制实现 (Phase 0 验证)
 *
 * 协作式抢占的安全点定义和实现。
 */

#include "safepoint.h"
#include <stdlib.h>
#include <time.h>

/* 获取当前时间（纳秒） */
static uint64_t get_time_ns(void) {
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
    ctx->last_yield_time = 0;
    ctx->yield_count = 0;

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

/* 内部让出函数 */
void safepoint_do_yield(safepoint_ctx_t *ctx) {
    if (!ctx) {
        return;
    }

    ctx->last_yield_time = get_time_ns();
    ctx->yield_count++;
}