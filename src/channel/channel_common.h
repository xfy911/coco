/**
 * channel_common.h - Channel 等待队列共享辅助函数
 *
 * 提供协程等待队列的入队/出队操作，供 channel.c 和 channel_mt.c 共享。
 */

#ifndef CHANNEL_COMMON_H
#define CHANNEL_COMMON_H

#include "../coco_internal.h"

/**
 * enqueue_wait_coro - 添加协程到等待队列尾部
 *
 * @param head 等待队列头指针
 * @param tail 等待队列尾指针
 * @param coro 要添加的协程
 */
static inline void enqueue_wait_coro(coco_coro_t **head, coco_coro_t **tail, coco_coro_t *coro) {
    coro->wait_node.next_waiter = NULL;
    if (*tail) {
        (*tail)->wait_node.next_waiter = coro;
    } else {
        *head = coro;
    }
    *tail = coro;
    coro->wait_node.in_use = true;
}

/**
 * dequeue_wait_coro - 从等待队列头部取出协程
 *
 * @param head 等待队列头指针
 * @param tail 等待队列尾指针
 * @return 队列头部的协程，如果队列为空返回 NULL
 */
static inline coco_coro_t *dequeue_wait_coro(coco_coro_t **head, coco_coro_t **tail) {
    coco_coro_t *coro = *head;
    if (!coro) {
        return NULL;
    }
    *head = coro->wait_node.next_waiter;
    if (!*head) {
        *tail = NULL;
    }
    coro->wait_node.in_use = false;
    coro->wait_node.next_waiter = NULL;
    return coro;
}

#endif /* CHANNEL_COMMON_H */