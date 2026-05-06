/**
 * global_sched.c - 全局调度器框架实现 (Phase 1)
 *
 * M:N 多线程调度架构，参考 Go runtime 设计。
 */

#include "global_sched.h"
#include "runq.h"
#include <time.h>
#include "../core/stack_pool_multi.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 全局调度器实例 */
static coco_global_sched_t *g_global_sched = NULL;

/* 获取 CPU 核心数 */
static uint32_t get_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0) ? (uint32_t)count : 1;
}

/* 创建处理器 (P) */
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

    atomic_init(&p->status, P_IDLE);
    p->next = NULL;

    return p;
}

/* 销毁处理器 (P) */
void coco_processor_destroy(coco_processor_t *p) {
    if (!p) {
        return;
    }

    pthread_mutex_destroy(&p->local_runq_lock);

    if (p->stack_pool) {
        stack_pool_multi_destroy(p->stack_pool);
    }

    free(p);
}

/* 创建机器 (M) */
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

/* 销毁机器 (M) */
void coco_machine_destroy(coco_machine_t *m) {
    if (m) {
        free(m);
    }
}

/* 全局初始化 */
int coco_global_init(uint32_t num_procs) {
    if (g_global_sched != NULL) {
        return -1;  /* 已初始化 */
    }

    if (num_procs == 0) {
        num_procs = get_cpu_count();
    }

    g_global_sched = calloc(1, sizeof(coco_global_sched_t));
    if (!g_global_sched) {
        return -1;
    }

    /* 初始化处理器数组 */
    g_global_sched->processors = calloc(num_procs, sizeof(coco_processor_t*));
    if (!g_global_sched->processors) {
        free(g_global_sched);
        g_global_sched = NULL;
        return -1;
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
            return -1;
        }
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

/* 全局销毁 */
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

/* 获取全局调度器 */
coco_global_sched_t *coco_global_get(void) {
    return g_global_sched;
}

/* 全局队列入队 */
int coco_global_runq_put(struct coco_coro *g) {
    if (!g_global_sched || !g) {
        return -1;
    }

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
    return 0;
}

/* 全局队列出队 */
struct coco_coro *coco_global_runq_get(void) {
    if (!g_global_sched) {
        return NULL;
    }

    pthread_mutex_lock(&g_global_sched->global_runq_lock);

    if (!g_global_sched->global_runq_head) {
        pthread_mutex_unlock(&g_global_sched->global_runq_lock);
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
    return g;
}

/* 全局队列大小 */
uint64_t coco_global_runq_size(void) {
    if (!g_global_sched) {
        return 0;
    }

    pthread_mutex_lock(&g_global_sched->global_runq_lock);
    uint64_t size = g_global_sched->global_runq_size;
    pthread_mutex_unlock(&g_global_sched->global_runq_lock);

    return size;
}

/* 获取处理器数量 */
uint32_t coco_processor_count(void) {
    return g_global_sched ? g_global_sched->processor_count : 0;
}

/* 获取指定处理器 */
coco_processor_t *coco_processor_get(uint32_t id) {
    if (!g_global_sched || id >= g_global_sched->processor_count) {
        return NULL;
    }
    return g_global_sched->processors[id];
}

/* Get next runnable coroutine: local queue -> global queue -> work steal */
static coco_coro_t *get_next_runnable(coco_processor_t *p) {
    coco_coro_t *coro;

    /* 1. Try local queue */
    coro = runq_get(p);
    if (coro) return coro;

    /* 2. Try global queue */
    coro = coco_global_runq_get();
    if (coro) return coro;

    /* 3. Try work stealing */
    coco_global_sched_t *gs = coco_global_get();
    if (!gs) return NULL;

    uint32_t start = rand() % gs->processor_count;
    for (uint32_t i = 0; i < gs->processor_count; i++) {
        uint32_t target_id = (start + i) % gs->processor_count;
        if (target_id == p->id) continue;

        coco_processor_t *target = gs->processors[target_id];
        if (!target) continue;

        coro = runq_steal(target);
        if (coro) return coro;
    }

    return NULL;
}

/* Worker thread loop */
static void *worker_loop(void *arg) {
    coco_processor_t *p = (coco_processor_t *)arg;
    coco_global_sched_t *gs = coco_global_get();

    /* Thread-local state for worker threads */
    extern _Thread_local coco_coro_t *g_current_coro;

    /* Set return context for this worker thread */
    g_return_ctx = &p->m->ctx;

    while (atomic_load(&gs->running)) {
        coco_coro_t *coro = get_next_runnable(p);
        if (coro) {
            atomic_store(&p->curcoro, coro);
            g_current_coro = coro;
            coco_ctx_switch(&p->m->ctx, &coro->ctx);
            g_current_coro = NULL;
            atomic_store(&p->curcoro, NULL);

            /* Handle coroutine state after switch back */
            if (coro->state == COCO_STATE_DEAD) {
                atomic_fetch_sub(&gs->active_coroutines, 1);
            }
        } else {
            /* Idle wait */
            pthread_mutex_lock(&gs->idle_lock);
            if (!atomic_load(&gs->running)) {
                pthread_mutex_unlock(&gs->idle_lock);
                break;
            }
            pthread_cond_wait(&gs->idle_cond, &gs->idle_lock);
            pthread_mutex_unlock(&gs->idle_lock);
        }
    }
    return NULL;
}

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

    atomic_store(&gs->running, true);
    atomic_init(&gs->next_coro_id, 0);

    /* Create M (worker threads) for each P */
    for (uint32_t i = 0; i < gs->processor_count; i++) {
        coco_processor_t *p = gs->processors[i];
        coco_machine_t *m = coco_machine_create(i);
        if (!m) {
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
            atomic_store(&gs->running, false);
            return COCO_ERROR;
        }
    }

    return COCO_OK;
}

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

int coco_global_sched_stop(void) {
    coco_global_sched_t *gs = coco_global_get();
    if (!gs) return COCO_ERROR;

    atomic_store(&gs->running, false);

    /* Wake up all idle workers */
    pthread_mutex_lock(&gs->idle_lock);
    pthread_cond_broadcast(&gs->idle_cond);
    pthread_mutex_unlock(&gs->idle_lock);

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

    return COCO_OK;
}