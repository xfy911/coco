/**
 * coro_go.c - coco_go API 实现 (US-017)
 *
 * 提供简洁的协程启动 API
 */

#include "coro_go.h"
#include "../sched/global_sched.h"
#include "../sched/runq.h"
#include "../core/stack_pool_multi.h"
#include <stdlib.h>
#include <stdatomic.h>

/* 外部声明 - 在 coro.c 中定义 */
extern _Thread_local coco_sched_t *g_current_sched;
extern void coro_entry_wrapper(void *arg);

/* 外部声明 - 在 safety.c 中定义 */
extern coco_safety_mode_t g_safety_mode;

/* 选择最佳 P */
static int select_best_p(void) {
    coco_global_sched_t *gs = coco_global_get();
    if (!gs || gs->processor_count == 0) {
        return -1;
    }

    /* 优先选择当前线程绑定的 P */
    /* TODO: 实现当前线程 P 绑定检测 */

    /* 选择队列最短的 P */
    uint32_t min_size = UINT32_MAX;
    int best_p = 0;

    for (uint32_t i = 0; i < gs->processor_count; i++) {
        coco_processor_t *p = gs->processors[i];
        if (p && p->local_runq_size < min_size) {
            min_size = p->local_runq_size;
            best_p = (int)i;
        }
    }

    return best_p;
}

/* coco_go - 自动选择最佳 P */
coco_coro_t *coco_go(void (*entry)(void*), void *arg) {
    return coco_go_with_opts(entry, arg, NULL);
}

/* coco_go_on - 在指定 P 上启动 */
coco_coro_t *coco_go_on(int p_id, void (*entry)(void*), void *arg) {
    coco_go_opts_t opts = {
        .stack_size = 0,
        .context = NULL,
        .priority = -1,
        .p_id = p_id
    };
    return coco_go_with_opts(entry, arg, &opts);
}

/* coco_go_with_opts - 带选项启动 */
coco_coro_t *coco_go_with_opts(void (*entry)(void*), void *arg,
                                const coco_go_opts_t *opts) {
    size_t stack_size = COCO_DEFAULT_STACK_SIZE;
    int priority = COCO_PRIORITY_NORMAL;
    int p_id = -1;

    /* 解析选项 */
    if (opts) {
        if (opts->stack_size > 0) {
            stack_size = opts->stack_size;
        }
        if (opts->priority >= 0 && opts->priority < COCO_PRIORITY_COUNT) {
            priority = opts->priority;
        }
        p_id = opts->p_id;
    }

    /* 检查是否有全局调度器 */
    coco_global_sched_t *gs = coco_global_get();
    if (gs && gs->processor_count > 0) {
        /* 多线程调度器 */
        if (p_id < 0) {
            p_id = select_best_p();
        }

        if (p_id < 0 || p_id >= (int)gs->processor_count) {
            return NULL;
        }

        coco_processor_t *p = gs->processors[p_id];
        if (!p) {
            return NULL;
        }

        /* 在指定 P 上创建协程 */
        /* Multi-threaded: create coroutine on target P */
        void *stack_top = stack_pool_multi_alloc((stack_pool_multi_t *)p->stack_pool, stack_size);
        if (!stack_top) {
            return NULL;
        }

        /* 获取实际分配的栈大小（栈池会向上对齐到 size class） */
        int class_idx = stack_pool_multi_get_class_index(stack_size);
        size_t actual_stack_size = (class_idx >= 0) ?
            stack_pool_multi_get_class_size(class_idx) : stack_size;

        coco_coro_t *coro = calloc(1, sizeof(coco_coro_t));
        if (!coro) {
            stack_pool_multi_free((stack_pool_multi_t *)p->stack_pool, stack_top, stack_size);
            return NULL;
        }

        /* Initialize coroutine fields (mirroring coco_create) */
        coro->stack_top = stack_top;
        coro->stack_base = (void *)((uintptr_t)stack_top - actual_stack_size - 4096);
        coro->stack_size = actual_stack_size;
        coro->entry = entry;
        coro->arg = arg;
        coro->id = atomic_fetch_add(&gs->next_coro_id, 1);
        coro->state = COCO_STATE_READY;
        coro->priority = (coco_priority_t)priority;
        coro->wait_fd = -1;
        coro->safety_mode = g_safety_mode;

        /* Initialize context (uses coro_entry_wrapper like coco_create) */
        coco_ctx_init(&coro->ctx, coro->stack_top, coro_entry_wrapper, arg);

        /* Add to P's local queue, overflow to global queue */
        if (runq_put(p, coro) != 0) {
            coco_global_runq_put(coro);
        }

        /* Wake idle workers - broadcast to all for high throughput */
        pthread_mutex_lock(&gs->idle_lock);
        pthread_cond_broadcast(&gs->idle_cond);
        pthread_mutex_unlock(&gs->idle_lock);

        /* Increment active coroutine count */
        atomic_fetch_add(&gs->active_coroutines, 1);

        /* Set context if provided */
        if (opts && opts->context) {
            coro->context = opts->context;
        }

        return coro;
    }

    /* 单线程调度器 */
    if (!g_current_sched) {
        /* 自动创建调度器 */
        g_current_sched = coco_sched_create();
        if (!g_current_sched) {
            return NULL;
        }
    }

    coco_coro_t *coro = coco_create(g_current_sched, entry, arg, stack_size);
    if (!coro) {
        return NULL;
    }

    /* 设置优先级 */
    if (priority != COCO_PRIORITY_NORMAL) {
        coco_set_priority(coro, (coco_priority_t)priority);
    }

    /* 设置 context */
    if (opts && opts->context) {
        coro->context = opts->context;
    }

    return coro;
}
