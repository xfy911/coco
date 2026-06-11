/**
 * runq.c - 本地运行队列实现 (Phase 1)
 *
 * Per-P 本地队列，支持工作窃取。
 *
 * 锁顺序: 必须按此顺序获取锁以防止死锁
 * 1. global_runq_lock (如果需要)
 * 2. local_runq_lock (如果需要)
 *
 * 永远不要在持有 local_runq_lock 时获取 global_runq_lock
 */

#include "runq.h"
#include "../core/hot_stack.h"
#include <string.h>


/**
 * 本地运行队列入队
 */
int runq_put(coco_processor_t *p, coco_coro_t *g) {
    if (!p || !g) {
        return -1;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&p->local_runq_lock);

    /* 检查是否溢出 */
    if (atomic_load(&p->local_runq_size) >= LOCAL_RUNQ_MAX) {
        pthread_mutex_unlock(&p->local_runq_lock);
        coco_preempt_unblock_signal();
        /* 溢出到全局队列 (先释放 local 锁，避免锁嵌套) */
        return runq_put_global(g);
    }

    /* 添加到队列尾部 */
    g->next = NULL;
    g->prev = p->local_runq_tail;

    if (p->local_runq_tail) {
        p->local_runq_tail->next = g;
    } else {
        p->local_runq_head = g;
    }
    p->local_runq_tail = g;
    atomic_fetch_add(&p->local_runq_size, 1);

    pthread_mutex_unlock(&p->local_runq_lock);
    coco_preempt_unblock_signal();
    return 0;
}

/**
 * 本地运行队列出队
 */
coco_coro_t *runq_get(coco_processor_t *p) {
    if (!p) {
        return NULL;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&p->local_runq_lock);

    if (!p->local_runq_head) {
        pthread_mutex_unlock(&p->local_runq_lock);
        coco_preempt_unblock_signal();
        return NULL;
    }

    coco_coro_t *g = p->local_runq_head;
    p->local_runq_head = g->next;

    if (p->local_runq_head) {
        p->local_runq_head->prev = NULL;
    } else {
        p->local_runq_tail = NULL;
    }

    g->next = NULL;
    g->prev = NULL;
    atomic_fetch_sub(&p->local_runq_size, 1);

    pthread_mutex_unlock(&p->local_runq_lock);
    coco_preempt_unblock_signal();
    return g;
}

/**
 * 工作窃取 - 从目标 P 偷取一半协程
 */
coco_coro_t *runq_steal(coco_processor_t *target) {
    if (!target) {
        return NULL;
    }

    /* 使用 trylock 减少阻塞 */
    coco_preempt_block_signal();
    if (pthread_mutex_trylock(&target->local_runq_lock) != 0) {
        coco_preempt_unblock_signal();
        return NULL;  /* 锁竞争，跳过这个 P */
    }

    if (atomic_load(&target->local_runq_size) == 0) {
        pthread_mutex_unlock(&target->local_runq_lock);
        coco_preempt_unblock_signal();
        return NULL;
    }

    /* 偷取一半 */
    uint32_t steal = atomic_load(&target->local_runq_size) / 2;
    if (steal == 0) steal = 1;

    coco_coro_t *batch = NULL;
    uint32_t count = 0;

    /* 从尾部偷取 */
    for (uint32_t i = 0; i < steal && target->local_runq_tail; i++) {
        coco_coro_t *g = target->local_runq_tail;
        target->local_runq_tail = g->prev;

        if (target->local_runq_tail) {
            target->local_runq_tail->next = NULL;
        } else {
            target->local_runq_head = NULL;
        }

        /* 添加到 batch 链表头部 */
        g->prev = NULL;
        g->next = batch;
        batch = g;

        atomic_fetch_sub(&target->local_runq_size, 1);
        count++;
    }

    pthread_mutex_unlock(&target->local_runq_lock);
    coco_preempt_unblock_signal();

    return batch;
}

/**
 * 将协程溢出到全局队列
 */
int runq_put_global(coco_coro_t *g) {
    return coco_global_runq_put(g);
}

/**
 * 负载均衡: 将本地队列尾部一半推入全局队列
 * 调用者必须已持有 local_runq_lock
 */
int runq_push_overflow(coco_processor_t *p) {
    if (!p || p->local_runq_size < LOCAL_RUNQ_MAX / 2) {
        return -1;
    }

    coco_sched_t *from = g_current_sched;

    uint32_t push = p->local_runq_size / 2;
    int pushed = 0;

    for (uint32_t i = 0; i < push && p->local_runq_tail; i++) {
        coco_coro_t *g = p->local_runq_tail;
        p->local_runq_tail = g->prev;

        if (p->local_runq_tail) {
            p->local_runq_tail->next = NULL;
        } else {
            p->local_runq_head = NULL;
        }

        g->prev = NULL;
        g->next = NULL;
        atomic_fetch_sub_explicit(&p->local_runq_size, 1, memory_order_relaxed);

        coro_migrate_prepare(from, g);

        if (runq_put_global(g) == 0) {
            pushed++;
        }
    }

    return pushed;
}

/**
 * 查询本地队列大小
 */
uint32_t runq_size(coco_processor_t *p) {
    if (!p) {
        return 0;
    }

    return atomic_load(&p->local_runq_size);
}

/**
 * 检查本地队列是否为空
 */
bool runq_empty(coco_processor_t *p) {
    return runq_size(p) == 0;
}