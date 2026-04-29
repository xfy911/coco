/**
 * locked_queue.h - 锁保护队列原型 (Phase 0 验证)
 *
 * 简单的互斥锁保护队列，用于验证多线程调度可行性。
 * 后续将扩展为 Per-P 本地队列 + 全局队列。
 */

#ifndef LOCKED_QUEUE_H
#define LOCKED_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* 队列节点 - 用于测试原型 */
typedef struct queue_node {
    void *data;                 /* 节点数据 */
    struct queue_node *next;    /* 下一个节点 */
    struct queue_node *prev;    /* 上一个节点 */
} queue_node_t;

/* 锁保护队列 */
typedef struct locked_queue {
    queue_node_t *head;         /* 队列头部 */
    queue_node_t *tail;         /* 队列尾部 */
    uint64_t size;              /* 队列大小 */
    pthread_mutex_t lock;       /* 互斥锁 */

    /* 统计信息 */
    uint64_t enqueue_count;     /* 入队次数 */
    uint64_t dequeue_count;     /* 出队次数 */
    uint64_t steal_count;       /* 偷取次数 */
} locked_queue_t;

/* API */
locked_queue_t *locked_queue_create(void);
void locked_queue_destroy(locked_queue_t *q);

/* 入队 */
int locked_queue_enqueue(locked_queue_t *q, queue_node_t *node);

/* 出队 */
queue_node_t *locked_queue_dequeue(locked_queue_t *q);

/* 偷取 (从尾部偷取一半) */
queue_node_t *locked_queue_steal(locked_queue_t *q, uint32_t *stolen_count);

/* 批量入队 (用于偷取后批量添加) */
int locked_queue_enqueue_batch(locked_queue_t *q, queue_node_t *batch, uint32_t count);

/* 查询 */
uint64_t locked_queue_size(locked_queue_t *q);
bool locked_queue_empty(locked_queue_t *q);

/* 统计 */
void locked_queue_get_stats(locked_queue_t *q,
                             uint64_t *enqueue_count,
                             uint64_t *dequeue_count,
                             uint64_t *steal_count);

#endif /* LOCKED_QUEUE_H */