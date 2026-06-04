/**
 * sched.c - 工作窃取调度器 (Phase 1, US-006)
 *
 * 实现 schedule() 主循环和 find_runnable() 查找可运行协程。
 *
 * 调度策略:
 * 1. 本地队列优先
 * 2. 全局队列次之
 * 3. 工作窃取最后
 */

#include "sched.h"
#include "runq.h"
#include "global_sched.h"
#include "../core/hot_stack.h"
#include <stdlib.h>
#include <string.h>

/* 调度参数 */
#define MAX_SPINS 10
#define STEAL_ATTEMPTS_MIN 2
#define STEAL_ATTEMPTS_MAX 6
#define STEAL_BACKOFF_THRESHOLD 3  /* 连续失败次数超过此值则退避 */

extern int coco_preempt_block_signal(void);
extern int coco_preempt_unblock_signal(void);

/* 线程局部窃取退避计数器 */
static _Thread_local uint32_t tl_steal_fail_count = 0;

/* 查找可运行协程 */
coco_coro_t *find_runnable(coco_processor_t *p) {
    if (!p) {
        return NULL;
    }

    coco_coro_t *g = NULL;

    /* 1. 本地队列 */
    g = runq_get(p);
    if (g) {
        /* 成功从本地获取，重置退避计数 */
        tl_steal_fail_count = 0;
        return g;
    }

    /* 2. 全局队列 */
    g = coco_global_runq_get();
    if (g) {
        tl_steal_fail_count = 0;
        coco_sched_t *from = coco_global_get()->main_sched;
        coro_migrate_prepare(from, g);
        return g;
    }

    /* 3. 工作窃取 — 自适应尝试次数 */
    coco_global_sched_t *sched = coco_global_get();
    if (!sched) {
        return NULL;
    }

    /* 根据失败历史动态调整尝试次数 */
    uint32_t steal_attempts;
    if (tl_steal_fail_count >= STEAL_BACKOFF_THRESHOLD) {
        /* 连续失败，减少尝试并增加随机性 */
        steal_attempts = STEAL_ATTEMPTS_MIN;
    } else {
        /* 正常情况 */
        steal_attempts = STEAL_ATTEMPTS_MAX;
    }

    /* 随机起始点，避免所有 P 同时偷取同一个目标 */
    uint32_t start = (p->id + 1) % sched->processor_count;
    uint32_t attempts = 0;
    uint32_t failed = 0;

    for (uint32_t i = 0; i < sched->processor_count && attempts < steal_attempts; i++) {
        uint32_t target_id = (start + i) % sched->processor_count;
        if (target_id == p->id) {
            continue;
        }

        coco_processor_t *target = coco_processor_get(target_id);
        if (!target || target->status != P_RUNNING) {
            continue;
        }

        g = runq_steal(target);
        if (g) {
            tl_steal_fail_count = 0;
            record_steal_attempt();
            record_steal_success();

            coco_sched_t *from = sched->main_sched;
            coro_migrate_prepare(from, g);

            coco_coro_t *next = g->next;
            if (next) {
                g->next = NULL;
                g->prev = NULL;
                while (next) {
                    coco_coro_t *n = next->next;
                    next->next = NULL;
                    next->prev = NULL;
                    coro_migrate_prepare(from, next);
                    runq_put(p, next);
                    next = n;
                }
            }
            return g;
        }
        attempts++;
        failed++;
    }

    /* 更新失败计数 */
    tl_steal_fail_count += failed;
    record_steal_attempt();
    return NULL;
}

/* 调度循环 (单次迭代) */
coco_coro_t *schedule_once(coco_processor_t *p) {
    if (!p) {
        return NULL;
    }

    /* 查找可运行协程 */
    coco_coro_t *g = find_runnable(p);
    if (!g) {
        return NULL;
    }

    /* 设置当前协程 */
    atomic_store(&p->curcoro, g);

    return g;
}

/* 完成协程执行 */
void schedule_done(coco_processor_t *p, coco_coro_t *g) {
    if (!p || !g) {
        return;
    }

    atomic_store(&p->curcoro, NULL);
}

/* 协程让出 (重新入队) */
void schedule_yield(coco_processor_t *p, coco_coro_t *g) {
    if (!p || !g) {
        return;
    }

    atomic_store(&p->curcoro, NULL);
    runq_put(p, g);
}

/* 协程阻塞 (不重新入队) */
void schedule_block(coco_processor_t *p, coco_coro_t *g) {
    if (!p || !g) {
        return;
    }

    atomic_store(&p->curcoro, NULL);
    /* 协程被放入等待队列，不重新入队 */
}

/* 协程唤醒 (放入全局队列) */
void schedule_ready(coco_coro_t *g) {
    if (!g) {
        return;
    }

    coco_global_runq_put(g);
}

/* 负载均衡: 从过载处理器迁移协程到全局队列 */
bool schedule_balanced(coco_global_sched_t *sched) {
    if (!sched || sched->processor_count == 0) {
        return true;
    }

    /* 计算平均负载 */
    uint64_t total = coco_global_runq_size();
    for (uint32_t i = 0; i < sched->processor_count; i++) {
        coco_processor_t *p = coco_processor_get(i);
        if (p) {
            total += runq_size(p);
        }
    }
    double avg = (double)total / sched->processor_count;
    if (avg < 4.0) {
        return true;  /* 负载不足，无需均衡 */
    }

    /* Phase 1: 在 trylock 下从过载 P 收集协程到临时链表 */
    coco_coro_t *to_move_head = NULL;
    coco_coro_t *to_move_tail = NULL;
    uint32_t moved_count = 0;

    for (uint32_t i = 0; i < sched->processor_count; i++) {
        coco_processor_t *p = coco_processor_get(i);
        if (!p) {
            continue;
        }

        uint32_t size = runq_size(p);
        if (size <= avg * 1.5) {
            continue;  /* 未过载 */
        }

        uint32_t target = (uint32_t)(avg * 1.2);
        uint32_t n = (size > target) ? (size - target) : 0;
        if (n == 0) {
            continue;
        }

        if (pthread_mutex_trylock(&p->local_runq_lock) != 0) {
            continue;  /* 锁竞争，跳过此 P */
        }

        for (uint32_t j = 0; j < n; j++) {
            /* 直接从队列头部取出（与 runq_get 逻辑一致，但已持有锁） */
            coco_coro_t *g = p->local_runq_head;
            if (!g) {
                break;
            }
            p->local_runq_head = g->next;
            if (p->local_runq_head) {
                p->local_runq_head->prev = NULL;
            } else {
                p->local_runq_tail = NULL;
            }
            atomic_fetch_sub_explicit(&p->local_runq_size, 1, memory_order_relaxed);

            /* 从原链表分离并加入临时链表 */
            g->prev = NULL;
            g->next = NULL;
            if (to_move_tail) {
                to_move_tail->next = g;
                to_move_tail = g;
            } else {
                to_move_head = to_move_tail = g;
            }
            moved_count++;
        }
        pthread_mutex_unlock(&p->local_runq_lock);
    }

    if (moved_count == 0) {
        return true;
    }

    /* 释放热栈槽位（如果有） */
    if (sched->main_sched) {
        for (coco_coro_t *g = to_move_head; g; g = g->next) {
            coro_migrate_prepare(sched->main_sched, g);
        }
    }

    /* Phase 2: 批量插入全局队列（不持有任何 local 锁） */

    coco_preempt_block_signal();
    pthread_mutex_lock(&sched->global_runq_lock);

    for (coco_coro_t *g = to_move_head; g;) {
        coco_coro_t *next = g->next;
        g->next = NULL;
        g->prev = sched->global_runq_tail;
        if (sched->global_runq_tail) {
            sched->global_runq_tail->next = g;
        } else {
            sched->global_runq_head = g;
        }
        sched->global_runq_tail = g;
        g = next;
    }
    sched->global_runq_size += moved_count;

    pthread_mutex_unlock(&sched->global_runq_lock);
    coco_preempt_unblock_signal();

    /* 唤醒空闲 worker 从全局队列窃取 */
    uint32_t idle = atomic_load(&sched->idle_count);
    if (idle > 0) {
        uint32_t to_wake = (moved_count < idle) ? moved_count : idle;
        coco_preempt_block_signal();
        pthread_mutex_lock(&sched->idle_lock);
        for (uint32_t i = 0; i < to_wake; i++) {
            pthread_cond_signal(&sched->idle_cond);
        }
        pthread_mutex_unlock(&sched->idle_lock);
        coco_preempt_unblock_signal();
    }

    return true;
}

/* 统计偷取成功率 */
typedef struct {
    uint64_t steal_attempts;
    uint64_t steal_success;
} steal_stats_t;

static _Thread_local steal_stats_t tl_steal_stats = {0, 0};

void record_steal_attempt(void) {
    tl_steal_stats.steal_attempts++;
}

void record_steal_success(void) {
    tl_steal_stats.steal_success++;
}

double get_steal_success_rate(void) {
    if (tl_steal_stats.steal_attempts == 0) {
        return 0.0;
    }
    return (double)tl_steal_stats.steal_success / tl_steal_stats.steal_attempts;
}
