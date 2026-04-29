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
#include <stdlib.h>
#include <string.h>

/* 调度参数 */
#define MAX_SPINS 10
#define STEAL_ATTEMPTS 3

/* 查找可运行协程 */
coco_coro_t *find_runnable(coco_processor_t *p) {
    if (!p) {
        return NULL;
    }

    coco_coro_t *g = NULL;

    /* 1. 本地队列 */
    g = runq_get(p);
    if (g) {
        return g;
    }

    /* 2. 全局队列 */
    g = coco_global_runq_get();
    if (g) {
        return g;
    }

    /* 3. 工作窃取 */
    coco_global_sched_t *sched = coco_global_get();
    if (!sched) {
        return NULL;
    }

    /* 随机起始点，避免所有 P 同时偷取同一个目标 */
    uint32_t start = (p->id + 1) % sched->processor_count;
    uint32_t attempts = 0;

    for (uint32_t i = 0; i < sched->processor_count && attempts < STEAL_ATTEMPTS; i++) {
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
            /* 将偷取的批次放入本地队列，返回第一个 */
            coco_coro_t *next = g->next;
            if (next) {
                g->next = NULL;
                g->prev = NULL;
                while (next) {
                    coco_coro_t *n = next->next;
                    next->next = NULL;
                    next->prev = NULL;
                    runq_put(p, next);
                    next = n;
                }
            }
            return g;
        }
        attempts++;
    }

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

/* 负载均衡检查 */
bool schedule_balanced(coco_global_sched_t *sched) {
    if (!sched || sched->processor_count == 0) {
        return true;
    }

    /* 计算平均负载 */
    uint64_t total = 0;
    for (uint32_t i = 0; i < sched->processor_count; i++) {
        coco_processor_t *p = coco_processor_get(i);
        if (p) {
            total += runq_size(p);
        }
    }
    total += coco_global_runq_size();

    double avg = (double)total / sched->processor_count;
    if (avg == 0) {
        return true;
    }

    /* 检查方差 */
    double variance = 0;
    for (uint32_t i = 0; i < sched->processor_count; i++) {
        coco_processor_t *p = coco_processor_get(i);
        if (p) {
            double diff = runq_size(p) - avg;
            variance += diff * diff;
        }
    }
    variance /= sched->processor_count;

    /* 方差 < 平均值的 20% */
    return variance < (avg * 0.2 * avg * 0.2);
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
