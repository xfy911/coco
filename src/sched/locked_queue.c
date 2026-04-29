/**
 * locked_queue.c - 锁保护队列原型实现 (Phase 0 验证)
 *
 * 简单的互斥锁保护队列，用于验证多线程调度可行性。
 */

#include "locked_queue.h"
#include <stdlib.h>

/* 创建队列 */
locked_queue_t *locked_queue_create(void) {
    locked_queue_t *q = calloc(1, sizeof(locked_queue_t));
    if (!q) {
        return NULL;
    }

    q->head = NULL;
    q->tail = NULL;
    q->size = 0;

    /* 初始化互斥锁 */
    pthread_mutex_init(&q->lock, NULL);

    q->enqueue_count = 0;
    q->dequeue_count = 0;
    q->steal_count = 0;

    return q;
}

/* 销毁队列 */
void locked_queue_destroy(locked_queue_t *q) {
    if (!q) {
        return;
    }

    /* 清空队列 */
    pthread_mutex_lock(&q->lock);
    queue_node_t *node = q->head;
    while (node) {
        queue_node_t *next = node->next;
        /* 注意: 不释放 node->data，由调用者管理 */
        node = next;
    }
    pthread_mutex_unlock(&q->lock);

    pthread_mutex_destroy(&q->lock);
    free(q);
}

/* 入队 - 添加到尾部 */
int locked_queue_enqueue(locked_queue_t *q, queue_node_t *node) {
    if (!q || !node) {
        return -1;
    }

    pthread_mutex_lock(&q->lock);

    node->next = NULL;
    node->prev = q->tail;

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->size++;
    q->enqueue_count++;

    pthread_mutex_unlock(&q->lock);
    return 0;
}

/* 出队 - 从头部取出 */
queue_node_t *locked_queue_dequeue(locked_queue_t *q) {
    if (!q) {
        return NULL;
    }

    pthread_mutex_lock(&q->lock);

    if (!q->head) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    queue_node_t *node = q->head;
    q->head = node->next;

    if (q->head) {
        q->head->prev = NULL;
    } else {
        q->tail = NULL;
    }

    node->next = NULL;
    node->prev = NULL;
    q->size--;
    q->dequeue_count++;

    pthread_mutex_unlock(&q->lock);
    return node;
}

/* 偷取 - 从尾部偷取一半 */
queue_node_t *locked_queue_steal(locked_queue_t *q, uint32_t *stolen_count) {
    if (!q) {
        return NULL;
    }

    /* 使用 trylock 减少阻塞 */
    if (pthread_mutex_trylock(&q->lock) != 0) {
        return NULL;  /* 锁竞争，跳过 */
    }

    if (q->size == 0) {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    /* 偷取一半 */
    uint32_t steal = q->size / 2;
    if (steal == 0) steal = 1;

    queue_node_t *batch = NULL;
    uint32_t count = 0;

    /* 从尾部偷取 */
    for (uint32_t i = 0; i < steal && q->tail; i++) {
        queue_node_t *node = q->tail;
        q->tail = node->prev;

        if (q->tail) {
            q->tail->next = NULL;
        } else {
            q->head = NULL;
        }

        /* 添加到 batch 链表头部 */
        node->prev = NULL;
        node->next = batch;
        batch = node;

        q->size--;
        count++;
    }

    q->steal_count++;

    pthread_mutex_unlock(&q->lock);

    if (stolen_count) {
        *stolen_count = count;
    }

    return batch;
}

/* 批量入队 */
int locked_queue_enqueue_batch(locked_queue_t *q, queue_node_t *batch, uint32_t count) {
    if (!q || !batch || count == 0) {
        return -1;
    }

    pthread_mutex_lock(&q->lock);

    /* 将 batch 链表添加到尾部 */
    /* batch 是以头部为起始的链表，需要找到尾部 */
    queue_node_t *batch_head = batch;
    queue_node_t *batch_tail = batch;
    while (batch_tail->next) {
        batch_tail = batch_tail->next;
    }

    batch_head->prev = q->tail;

    if (q->tail) {
        q->tail->next = batch_head;
    } else {
        q->head = batch_head;
    }
    q->tail = batch_tail;
    q->size += count;
    q->enqueue_count += count;

    pthread_mutex_unlock(&q->lock);
    return 0;
}

/* 查询大小 */
uint64_t locked_queue_size(locked_queue_t *q) {
    if (!q) {
        return 0;
    }

    pthread_mutex_lock(&q->lock);
    uint64_t size = q->size;
    pthread_mutex_unlock(&q->lock);

    return size;
}

/* 查询是否为空 */
bool locked_queue_empty(locked_queue_t *q) {
    return locked_queue_size(q) == 0;
}

/* 获取统计 */
void locked_queue_get_stats(locked_queue_t *q,
                             uint64_t *enqueue_count,
                             uint64_t *dequeue_count,
                             uint64_t *steal_count) {
    if (!q) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    if (enqueue_count) *enqueue_count = q->enqueue_count;
    if (dequeue_count) *dequeue_count = q->dequeue_count;
    if (steal_count) *steal_count = q->steal_count;
    pthread_mutex_unlock(&q->lock);
}