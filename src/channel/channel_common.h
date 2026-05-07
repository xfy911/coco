/**
 * channel_common.h - Channel 等待队列共享辅助函数
 *
 * 提供协程等待队列的入队/出队操作，供 channel.c 和 channel_mt.c 共享。
 */

#ifndef CHANNEL_COMMON_H
#define CHANNEL_COMMON_H

#include "../coco_internal.h"
#include <stdatomic.h>
#include <pthread.h>

/**
 * enqueue_wait_coro - 添加协程到等待队列尾部
 *
 * @param head 等待队列头指针
 * @param tail 等待队列尾指针
 * @param coro 要添加的协程
 * @param ch 等待的 channel (void* 支持不同 channel 类型)
 */
static inline void enqueue_wait_coro(coco_coro_t **head, coco_coro_t **tail, coco_coro_t *coro, void *ch) {
    coro->wait_node.next_waiter = NULL;
    if (*tail) {
        (*tail)->wait_node.next_waiter = coro;
    } else {
        *head = coro;
    }
    *tail = coro;
    coro->wait_node.in_use = true;
    coro->wait_node.channel = ch;
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
    coro->wait_node.channel = NULL;
    return coro;
}

/**
 * coco_channel_remove_waiter - 从 channel 等待队列中移除指定协程（声明，实现在 channel.c）
 *
 * @param ch channel 指针
 * @param coro 要移除的协程
 *
 * 从发送队列或接收队列中移除协程，更新 tail 指针。
 * 调用者必须持有 wait_queue_lock。
 */
void coco_channel_remove_waiter(coco_channel_t *ch, coco_coro_t *coro);

/**
 * coco_channel_cancel_cleanup - 取消时清理 channel 等待（声明，实现在 channel.c）
 *
 * @param ch channel 指针
 * @param coro 要取消的协程
 *
 * 持锁从等待队列移除协程并减少引用计数。
 */
void coco_channel_cancel_cleanup(coco_channel_t *ch, coco_coro_t *coro);

/**
 * coco_channel_ref - 增加 channel 引用计数（声明，实现在 channel.c）
 *
 * @param ch channel 指针
 */
void coco_channel_ref(coco_channel_t *ch);

/**
 * coco_channel_unref - 减少 channel 引用计数，计数为 0 时释放（声明，实现在 channel.c）
 *
 * @param ch channel 指针
 * @return true 如果 channel 已释放，false 否则
 */
bool coco_channel_unref(coco_channel_t *ch);

#endif /* CHANNEL_COMMON_H */