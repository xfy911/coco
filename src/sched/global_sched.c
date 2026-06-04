/**
 * 全局调度器框架实现 (Phase 1)
 *
 * M:N 多线程调度架构，参考 Go runtime 设计。
 */

#include "global_sched.h"
#include "runq.h"
#include "sched.h"
#include <time.h>
#include "../core/stack_pool_multi.h"
#include "../io/netpoller_mt.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int coco_preempt_block_signal(void);
extern int coco_preempt_unblock_signal(void);

/* 全局调度器实例 */
static coco_global_sched_t *g_global_sched = NULL;

/**
 * 获取 CPU 核心数
 */
static uint32_t get_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0) ? (uint32_t)count : 1;
}

/**
 * 创建处理器 (P)
 */
coco_processor_t *coco_processor_create(uint32_t id) {
    coco_processor_t *p = calloc(1, sizeof(coco_processor_t));
    if (!p) {
        return NULL;
    }

    p->id = id;
    p->local_runq_head = NULL;
    p->local_runq_tail = NULL;
    p->local_runq_size = 0;
    pthread_mutex_init(&p->local_runq_lock, NULL);

    atomic_init(&p->curcoro, NULL);
    p->m = NULL;

    /* 创建 Per-P 栈池 */
    p->stack_pool = stack_pool_multi_create();
    if (!p->stack_pool) {
        pthread_mutex_destroy(&p->local_runq_lock);
        free(p);
        return NULL;
    }

    /* 创建 Per-P 时间轮 */
    p->timer_wheel = coco_timer_wheel_create();
    if (!p->timer_wheel) {
        stack_pool_multi_destroy(p->stack_pool);
        pthread_mutex_destroy(&p->local_runq_lock);
        free(p);
        return NULL;
    }

    p->global_sched = NULL;

    atomic_init(&p->status, P_IDLE);
    p->next = NULL;

    return p;
}

/**
 * 销毁处理器 (P)
 */
void coco_processor_destroy(coco_processor_t *p) {
    if (!p) {
        return;
    }

    pthread_mutex_destroy(&p->local_runq_lock);

    if (p->stack_pool) {
        stack_pool_multi_destroy(p->stack_pool);
    }

    if (p->timer_wheel) {
        coco_timer_wheel_destroy(p->timer_wheel);
    }

    free(p);
}

/**
 * 创建机器 (M)
 */
coco_machine_t *coco_machine_create(uint32_t id) {
    coco_machine_t *m = calloc(1, sizeof(coco_machine_t));
    if (!m) {
        return NULL;
    }

    m->id = id;
    m->thread = 0;
    m->p = NULL;
    atomic_init(&m->status, M_IDLE);
    m->next = NULL;

    return m;
}

/**
 * 销毁机器 (M)
 */
void coco_machine_destroy(coco_machine_t *m) {
    if (m) {
        free(m);
    }
}

/**
 * 全局调度器初始化
 */
int coco_global_init(uint32_t num_procs) {
    if (g_global_sched != NULL) {
        return COCO_ERROR;  /* 已初始化 */
    }

    if (num_procs == 0) {
        num_procs = get_cpu_count();
    }

    g_global_sched = calloc(1, sizeof(coco_global_sched_t));
    if (!g_global_sched) {
        return COCO_ERROR;
    }

    /* 初始化处理器数组 */
    g_global_sched->processors = calloc(num_procs, sizeof(coco_processor_t*));
    if (!g_global_sched->processors) {
        free(g_global_sched);
        g_global_sched = NULL;
        return COCO_ERROR;
    }

    g_global_sched->processor_count = num_procs;
    g_global_sched->processor_mask = num_procs - 1;

    /* 创建处理器 */
    for (uint32_t i = 0; i < num_procs; i++) {
        g_global_sched->processors[i] = coco_processor_create(i);
        if (!g_global_sched->processors[i]) {
            /* 清理已创建的处理器 */
            for (uint32_t j = 0; j < i; j++) {
                coco_processor_destroy(g_global_sched->processors[j]);
            }
            free(g_global_sched->processors);
            free(g_global_sched);
            g_global_sched = NULL;
            return COCO_ERROR;
        }
        /* 设置反向引用 */
        g_global_sched->processors[i]->global_sched = g_global_sched;
    }

    /* 初始化全局队列 */
    g_global_sched->global_runq_head = NULL;
    g_global_sched->global_runq_tail = NULL;
    g_global_sched->global_runq_size = 0;
    pthread_mutex_init(&g_global_sched->global_runq_lock, NULL);

    /* 初始化空闲列表 */
    g_global_sched->idle_processors = NULL;
    pthread_mutex_init(&g_global_sched->idle_lock, NULL);

    g_global_sched->idle_machines = NULL;
    pthread_cond_init(&g_global_sched->idle_cond, NULL);

    /* 初始化统计 */
    atomic_init(&g_global_sched->total_coroutines, 0);
    atomic_init(&g_global_sched->active_coroutines, 0);
    atomic_init(&g_global_sched->next_coro_id, 0);
    atomic_init(&g_global_sched->running, false);

    return 0;
}

/**
 * 全局调度器销毁
 */
void coco_global_destroy(void) {
    if (!g_global_sched) {
        return;
    }

    atomic_store(&g_global_sched->running, false);

    /* 销毁处理器 */
    for (uint32_t i = 0; i < g_global_sched->processor_count; i++) {
        coco_processor_destroy(g_global_sched->processors[i]);
    }

    pthread_mutex_destroy(&g_global_sched->global_runq_lock);
    pthread_mutex_destroy(&g_global_sched->idle_lock);
    pthread_cond_destroy(&g_global_sched->idle_cond);

    free(g_global_sched->processors);
    free(g_global_sched);
    g_global_sched = NULL;
}

/**
 * 获取全局调度器实例
 */
coco_global_sched_t *coco_global_get(void) {
    return g_global_sched;
}

/**
 * 全局运行队列入队
 */
int coco_global_runq_put(struct coco_coro *g) {
    if (!g_global_sched || !g) {
        return COCO_ERROR;
    }

    g->state = COCO_STATE_READY;  /* 设置就绪状态 */

    coco_preempt_block_signal();
    pthread_mutex_lock(&g_global_sched->global_runq_lock);

    g->next = NULL;
    g->prev = g_global_sched->global_runq_tail;

    if (g_global_sched->global_runq_tail) {
        g_global_sched->global_runq_tail->next = g;
    } else {
        g_global_sched->global_runq_head = g;
    }
    g_global_sched->global_runq_tail = g;
    g_global_sched->global_runq_size++;

    pthread_mutex_unlock(&g_global_sched->global_runq_lock);
    coco_preempt_unblock_signal();

    /* 按需唤醒空闲线程: 根据全局队列长度和可用 P 决定唤醒数量
     * 避免 thundering herd: 只唤醒最少数量的空闲线程 */
    int to_wake = (int)atomic_load(&g_global_sched->active_coroutines);
    if (to_wake == 0) to_wake = 1;  /* 至少唤醒一个 */
    if (to_wake > (int)g_global_sched->processor_count) {
        to_wake = g_global_sched->processor_count;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&g_global_sched->idle_lock);
    for (int i = 0; i < to_wake; i++) {
        pthread_cond_signal(&g_global_sched->idle_cond);
    }
    pthread_mutex_unlock(&g_global_sched->idle_lock);
    coco_preempt_unblock_signal();

    return 0;
}

/**
 * 全局运行队列出队
 */
struct coco_coro *coco_global_runq_get(void) {
    if (!g_global_sched) {
        return NULL;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&g_global_sched->global_runq_lock);

    if (!g_global_sched->global_runq_head) {
        pthread_mutex_unlock(&g_global_sched->global_runq_lock);
        coco_preempt_unblock_signal();
        return NULL;
    }

    struct coco_coro *g = g_global_sched->global_runq_head;
    g_global_sched->global_runq_head = g->next;

    if (g_global_sched->global_runq_head) {
        g_global_sched->global_runq_head->prev = NULL;
    } else {
        g_global_sched->global_runq_tail = NULL;
    }

    g->next = NULL;
    g->prev = NULL;
    g_global_sched->global_runq_size--;

    pthread_mutex_unlock(&g_global_sched->global_runq_lock);
    coco_preempt_unblock_signal();
    return g;
}

/**
 * 获取全局队列大小
 */
uint64_t coco_global_runq_size(void) {
    if (!g_global_sched) {
        return 0;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&g_global_sched->global_runq_lock);
    uint64_t size = g_global_sched->global_runq_size;
    pthread_mutex_unlock(&g_global_sched->global_runq_lock);
    coco_preempt_unblock_signal();

    return size;
}

/**
 * 获取处理器 (P) 数量
 */
uint32_t coco_processor_count(void) {
    return g_global_sched ? g_global_sched->processor_count : 0;
}

/**
 * 获取指定处理器 (P)
 */
coco_processor_t *coco_processor_get(uint32_t id) {
    if (!g_global_sched || id >= g_global_sched->processor_count) {
        return NULL;
    }
    return g_global_sched->processors[id];
}

/**
 * 工作线程主循环
 */
static void *worker_loop(void *arg) {
    coco_processor_t *p = (coco_processor_t *)arg;
    coco_global_sched_t *gs = coco_global_get();

    /* Thread-local state for worker threads */
    extern _Thread_local coco_coro_t *g_current_coro;
    extern _Thread_local coco_sched_t *g_current_sched;

    /* Set g_current_sched to main_sched for coco_yield/coco_read etc. */
    g_current_sched = gs->main_sched;

    /* Set return context for this worker thread */
    g_return_ctx = &p->m->ctx;

    while (atomic_load(&gs->running)) {
        /* 处理定时器 tick */
        coco_timer_tick(p->timer_wheel, gs->main_sched);

        coco_coro_t *coro = find_runnable(p);
        if (coro) {
            atomic_store(&p->curcoro, coro);
            g_current_coro = coro;
            coro->state = COCO_STATE_RUNNING;  /* 设置运行状态 */
            coco_ctx_switch(&p->m->ctx, &coro->ctx);
            g_current_coro = NULL;
            atomic_store(&p->curcoro, NULL);

            /* Handle coroutine state after switch back */
            if (coro->state == COCO_STATE_DEAD) {
                atomic_fetch_sub(&gs->active_coroutines, 1);
                /* Free dead coroutine */
                if (coro->stack_base && p->stack_pool) {
                    stack_pool_multi_free((stack_pool_multi_t *)p->stack_pool,
                                          coro->stack_top, coro->stack_size);
                    coro->stack_base = NULL;
                }
                free(coro);
            }

            /* Load balancing: push overflow from local to global queue
             * This prevents a single P from becoming a bottleneck */
            coco_preempt_block_signal();
            pthread_mutex_lock(&p->local_runq_lock);
            runq_push_overflow(p);
            pthread_mutex_unlock(&p->local_runq_lock);
            coco_preempt_unblock_signal();
        } else {
            /* Idle wait - 持锁重检查避免丢失唤醒 (H2) */
            coco_preempt_block_signal();
            pthread_mutex_lock(&gs->idle_lock);
            if (!atomic_load(&gs->running)) {
                pthread_mutex_unlock(&gs->idle_lock);
                coco_preempt_unblock_signal();
                break;
            }
            /* H2 fix: 持锁重检查 find_runnable，避免唤醒信号丢失 */
            coro = find_runnable(p);
            if (coro) {
                pthread_mutex_unlock(&gs->idle_lock);
                coco_preempt_unblock_signal();
                atomic_store(&p->curcoro, coro);
                g_current_coro = coro;
                coro->state = COCO_STATE_RUNNING;  /* 设置运行状态 */
                coco_ctx_switch(&p->m->ctx, &coro->ctx);
                g_current_coro = NULL;
                atomic_store(&p->curcoro, NULL);
                if (coro->state == COCO_STATE_DEAD) {
                    atomic_fetch_sub(&gs->active_coroutines, 1);
                    /* Free dead coroutine */
                    if (coro->stack_base && p->stack_pool) {
                        stack_pool_multi_free((stack_pool_multi_t *)p->stack_pool,
                                              coro->stack_top, coro->stack_size);
                        coro->stack_base = NULL;
                    }
                    free(coro);
                }
                continue;
            }
            /* 计算定时器超时 */
            uint64_t next_expire = coco_timer_wheel_next_expire(p->timer_wheel);
            struct timespec ts;
            if (next_expire > 0) {
                uint64_t now_ms = get_current_time_ms_internal();
                uint64_t wait_ms = (next_expire > now_ms) ? (next_expire - now_ms) : 1;
                ts.tv_sec = wait_ms / 1000;
                ts.tv_nsec = (wait_ms % 1000) * 1000000;
                pthread_cond_timedwait(&gs->idle_cond, &gs->idle_lock, &ts);
            } else {
                /* 无定时器，无限等待 */
                pthread_cond_wait(&gs->idle_cond, &gs->idle_lock);
            }
            pthread_mutex_unlock(&gs->idle_lock);
            coco_preempt_unblock_signal();
        }
    }
    return NULL;
}

/**
 * 启动全局调度器
 */
int coco_global_sched_start(uint32_t num_workers) {
    coco_global_sched_t *gs = coco_global_get();

    if (!gs) {
        /* Auto-initialize with num_workers processors */
        if (coco_global_init(num_workers) != 0) {
            return COCO_ERROR;
        }
        gs = coco_global_get();
    }

    if (atomic_load(&gs->running)) {
        return COCO_ERROR_INVALID;  /* Already running */
    }

    /* 创建主调度器 (用于定时器唤醒协程) */
    gs->main_sched = coco_sched_create();
    if (!gs->main_sched) {
        return COCO_ERROR_NOMEM;
    }

    /* 创建 netpoller (专用 I/O 轮询线程) */
    gs->netpoller = coco_netpoller_create(gs);
    if (!gs->netpoller) {
        coco_sched_destroy(gs->main_sched);
        gs->main_sched = NULL;
        return COCO_ERROR_NOMEM;
    }

    atomic_store(&gs->running, true);
    atomic_init(&gs->next_coro_id, 0);

    /* Create M (worker threads) for each P */
    for (uint32_t i = 0; i < gs->processor_count; i++) {
        coco_processor_t *p = gs->processors[i];
        coco_machine_t *m = coco_machine_create(i);
        if (!m) {
            coco_sched_destroy(gs->main_sched);
            gs->main_sched = NULL;
            atomic_store(&gs->running, false);
            return COCO_ERROR_NOMEM;
        }

        /* Bind M to P */
        p->m = m;
        m->p = p;
        atomic_store(&p->status, P_RUNNING);
        atomic_store(&m->status, M_RUNNING);

        /* Create worker thread */
        if (pthread_create(&m->thread, NULL, worker_loop, p) != 0) {
            coco_machine_destroy(m);
            p->m = NULL;
            coco_netpoller_destroy(gs->netpoller);
            gs->netpoller = NULL;
            coco_sched_destroy(gs->main_sched);
            gs->main_sched = NULL;
            atomic_store(&gs->running, false);
            return COCO_ERROR;
        }
    }

    /* 启动 netpoller 线程 */
    if (coco_netpoller_start(gs->netpoller) != COCO_OK) {
        /* 工作线程已启动，需要停止它们 */
        atomic_store(&gs->running, false);
        coco_preempt_block_signal();
        pthread_mutex_lock(&gs->idle_lock);
        pthread_cond_broadcast(&gs->idle_cond);
        pthread_mutex_unlock(&gs->idle_lock);
        coco_preempt_unblock_signal();
        for (uint32_t i = 0; i < gs->processor_count; i++) {
            coco_processor_t *p = gs->processors[i];
            if (p && p->m && p->m->thread) {
                pthread_join(p->m->thread, NULL);
            }
        }
        coco_netpoller_destroy(gs->netpoller);
        gs->netpoller = NULL;
        coco_sched_destroy(gs->main_sched);
        gs->main_sched = NULL;
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * 等待所有协程完成
 */
int coco_global_sched_wait(void) {
    coco_global_sched_t *gs = coco_global_get();
    if (!gs) return COCO_ERROR;

    /* Wait until all coroutines complete */
    while (atomic_load(&gs->active_coroutines) > 0 && atomic_load(&gs->running)) {
        struct timespec ts = {0, 10000000};  /* 10ms */
        nanosleep(&ts, NULL);
    }

    return COCO_OK;
}

/**
 * 停止全局调度器
 */
int coco_global_sched_stop(void) {
    coco_global_sched_t *gs = coco_global_get();
    if (!gs) return COCO_ERROR;

    atomic_store(&gs->running, false);

    /* Wake up all idle workers */
    coco_preempt_block_signal();
    pthread_mutex_lock(&gs->idle_lock);
    pthread_cond_broadcast(&gs->idle_cond);
    pthread_mutex_unlock(&gs->idle_lock);
    coco_preempt_unblock_signal();

    /* Wait for worker threads to finish */
    for (uint32_t i = 0; i < gs->processor_count; i++) {
        coco_processor_t *p = gs->processors[i];
        if (p && p->m && p->m->thread) {
            pthread_join(p->m->thread, NULL);
        }
    }

    /* Destroy M structures */
    for (uint32_t i = 0; i < gs->processor_count; i++) {
        coco_processor_t *p = gs->processors[i];
        if (p && p->m) {
            coco_machine_destroy(p->m);
            p->m = NULL;
            atomic_store(&p->status, P_IDLE);
        }
    }

    /* 停止并销毁 netpoller */
    if (gs->netpoller) {
        coco_netpoller_destroy(gs->netpoller);
        gs->netpoller = NULL;
    }

    /* Destroy main scheduler */
    if (gs->main_sched) {
        coco_sched_destroy(gs->main_sched);
        gs->main_sched = NULL;
    }

    return COCO_OK;
}