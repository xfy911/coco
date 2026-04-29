/**
 * sched_stats.c - 调度器统计 API 实现 (US-016)
 */

#include "sched_stats.h"
#include "../coco_internal.h"
#include "global_sched.h"
#include "../core/stack_pool.h"
#include <string.h>

/* 全局统计计数器（原子操作） */
static _Atomic uint64_t g_coro_total_created = 0;
static _Atomic uint64_t g_coro_current_alive = 0;

/* 外部声明 - 在 coro.c 中定义 */
extern _Thread_local coco_sched_t *g_current_sched;

/* 更新协程创建统计 */
void coco_stats_coro_created(void) {
    atomic_fetch_add(&g_coro_total_created, 1);
    atomic_fetch_add(&g_coro_current_alive, 1);
}

/* 更新协程完成统计 */
void coco_stats_coro_finished(void) {
    atomic_fetch_sub(&g_coro_current_alive, 1);
}

/* 获取单线程调度器统计 */
int coco_sched_get_stats(coco_sched_stats_t *stats) {
    if (!stats) return -1;

    if (!g_current_sched) {
        memset(stats, 0, sizeof(*stats));
        return -1;
    }

    stats->coroutines_created = atomic_load(&g_coro_total_created);
    stats->coroutines_finished = atomic_load(&g_coro_total_created) - atomic_load(&g_coro_current_alive);
    stats->context_switches = 0;  /* 单线程调度器暂无此统计 */
    stats->queue_size = g_current_sched->ready_count;

    return 0;
}

/* 获取多线程调度器统计所需的大小 */
size_t coco_global_sched_stats_size(void) {
    coco_global_sched_t *gs = coco_global_get();
    if (!gs) return sizeof(coco_global_sched_stats_t);

    return sizeof(coco_global_sched_stats_t) +
           gs->processor_count * sizeof(uint32_t);
}

/* 获取多线程调度器统计 */
int coco_global_sched_get_stats(coco_global_sched_stats_t *stats) {
    if (!stats) return -1;

    coco_global_sched_t *gs = coco_global_get();
    if (!gs) {
        memset(stats, 0, sizeof(*stats));
        return -1;
    }

    stats->coroutines_created = atomic_load(&g_coro_total_created);
    stats->coroutines_finished = atomic_load(&g_coro_total_created) - atomic_load(&g_coro_current_alive);
    stats->context_switches = 0;  /* 需要从各 P 累加 */
    stats->steals_attempted = 0;  /* 暂无此统计 */
    stats->steals_succeeded = 0;  /* 暂无此统计 */
    stats->global_queue_size = gs->global_runq_size;
    stats->p_count = gs->processor_count;

    /* 收集各 P 的统计 */
    for (uint32_t i = 0; i < gs->processor_count; i++) {
        coco_processor_t *p = gs->processors[i];
        if (p) {
            stats->local_queue_sizes[i] = p->local_runq_size;
        } else {
            stats->local_queue_sizes[i] = 0;
        }
    }

    return 0;
}

/* 获取栈池统计 */
int coco_get_stack_stats(coco_stack_stats_t *stats) {
    if (!stats) return -1;

    memset(stats, 0, sizeof(*stats));

    /* 从单线程调度器的栈池获取统计 */
    if (g_current_sched && g_current_sched->stack_pool) {
        stack_pool_t *pool = g_current_sched->stack_pool;
        for (int i = 0; i < STACK_POOL_NUM_CLASSES; i++) {
            stats->total_allocs += pool->counts[i];
        }
        /* 栈池命中/未命中暂无统计 */
    }

    return 0;
}

/* 获取协程统计 */
int coco_get_coro_stats(coco_coro_stats_t *stats) {
    if (!stats) return -1;

    stats->total_created = atomic_load(&g_coro_total_created);
    stats->current_alive = atomic_load(&g_coro_current_alive);
    stats->avg_lifetime_ns = 0;  /* 需要额外追踪 */

    return 0;
}
