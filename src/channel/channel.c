/**
 * channel.c - Channel 实现
 *
 * 支持有缓冲（环形缓冲区）和无缓冲（同步传递）channel。
 * 使用嵌入式等待节点，避免动态内存分配。
 */

#include "../coco_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* 外部全局变量（TLS，在 coro.c 中定义） */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/* Channel 结构 */
struct coco_channel {
    size_t capacity;          /* 缓冲区大小（0 = 无缓冲） */
    int closed;               /* 是否已关闭 */

    /* 有缓冲 channel: 环形缓冲区 */
    void **buffer;
    size_t head;              /* 读位置 */
    size_t tail;              /* 写位置 */
    size_t count;             /* 当前元素数 */

    /* 等待队列（使用协程的嵌入式 wait_node） */
    coco_coro_t *send_wait_head;  /* 发送等待队列 */
    coco_coro_t *send_wait_tail;
    coco_coro_t *recv_wait_head;  /* 接收等待队列 */
    coco_coro_t *recv_wait_tail;
};

/* 添加协程到等待队列尾部 */
static void enqueue_wait_coro(coco_coro_t **head, coco_coro_t **tail, coco_coro_t *coro) {
    coro->wait_node.next_waiter = NULL;
    if (*tail) {
        (*tail)->wait_node.next_waiter = coro;
    } else {
        *head = coro;
    }
    *tail = coro;
    coro->wait_node.in_use = true;
}

/* 从等待队列头部取出协程 */
static coco_coro_t *dequeue_wait_coro(coco_coro_t **head, coco_coro_t **tail) {
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

/**
 * coco_channel_create - 创建 channel
 *
 * @param capacity 缓冲区大小（0 = 无缓冲）
 * @return channel 指针，失败返回 NULL
 */
coco_channel_t *coco_channel_create(size_t capacity) {
    coco_channel_t *ch = calloc(1, sizeof(coco_channel_t));
    if (!ch) {
        return NULL;
    }

    ch->capacity = capacity;
    ch->closed = 0;

    if (capacity > 0) {
        /* 有缓冲 channel: 分配环形缓冲区 */
        ch->buffer = calloc(capacity, sizeof(void*));
        if (!ch->buffer) {
            free(ch);
            return NULL;
        }
    }

    return ch;
}

/**
 * coco_channel_send - 发送数据（阻塞）
 *
 * @param ch channel 指针
 * @param value 数据指针
 * @return COCO_OK 成功，负数错误码
 */
int coco_channel_send(coco_channel_t *ch, void *value) {
    if (!ch || ch->closed) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    /* 检查是否有接收者在等待 */
    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        /* 直接传递给接收者 */
        recv_waiter->wait_node.value = value;
        enqueue_ready(sched, recv_waiter);
        return COCO_OK;
    }

    /* 有缓冲 channel: 尝试放入缓冲区 */
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = value;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return COCO_OK;
    }

    /* 无缓冲或缓冲区满: 阻塞等待 */
    if (coro->wait_node.in_use) {
        return COCO_ERROR;  /* 协程已在等待队列中 */
    }

    coro->wait_node.value = value;
    coro->wait_node.freed_by_destroy = false;
    enqueue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail, coro);
    coro->state = COCO_STATE_WAITING;
    coco_yield();

    /* 检查是否被取消 */
    if (coro->cancelled) {
        return COCO_ERROR_CANCELLED;
    }

    /* 恢复后检查 channel 是否关闭 */
    if (ch->closed) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    return COCO_OK;
}

/**
 * coco_channel_recv - 接收数据（阻塞）
 *
 * @param ch channel 指针
 * @param value 接收数据指针的指针
 * @return COCO_OK 成功，负数错误码
 */
int coco_channel_recv(coco_channel_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    if (ch->closed && ch->count == 0 && !ch->send_wait_head) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    /* 有缓冲 channel: 尝试从缓冲区取 */
    if (ch->capacity > 0 && ch->count > 0) {
        *value = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        /* 缓冲区有空间，唤醒发送者 */
        coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            ch->buffer[ch->tail] = send_waiter->wait_node.value;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            enqueue_ready(sched, send_waiter);
        }

        return COCO_OK;
    }

    /* 无缓冲 channel: 检查是否有发送者等待 */
    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->wait_node.value;
        enqueue_ready(sched, send_waiter);
        return COCO_OK;
    }

    /* 阻塞等待 */
    if (coro->wait_node.in_use) {
        return COCO_ERROR;  /* 协程已在等待队列中 */
    }

    coro->wait_node.value = NULL;
    coro->wait_node.freed_by_destroy = false;
    enqueue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail, coro);
    coro->state = COCO_STATE_WAITING;
    coco_yield();

    /* 检查是否被取消 */
    if (coro->cancelled) {
        return COCO_ERROR_CANCELLED;
    }

    /* 恢复后获取值 */
    if (ch->closed && !coro->wait_node.value) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    *value = coro->wait_node.value;
    return COCO_OK;
}

/**
 * coco_channel_close - 关闭 channel
 *
 * @param ch channel 指针
 */
void coco_channel_close(coco_channel_t *ch) {
    if (!ch || ch->closed) {
        return;
    }

    ch->closed = 1;
    coco_sched_t *sched = g_current_sched;

    /* 唤醒所有等待的接收者 */
    while (ch->recv_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (sched && coro) {
            enqueue_ready(sched, coro);
        }
    }

    /* 唤醒所有等待的发送者 */
    while (ch->send_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (sched && coro) {
            enqueue_ready(sched, coro);
        }
    }
}

/**
 * coco_channel_destroy - 销毁 channel
 *
 * @param ch channel 指针
 */
void coco_channel_destroy(coco_channel_t *ch) {
    if (!ch) {
        return;
    }

    if (ch->buffer) {
        free(ch->buffer);
    }

    /* 清理等待队列，设置 freed_by_destroy 标志 */
    while (ch->send_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (coro) {
            coro->wait_node.freed_by_destroy = true;
        }
    }
    while (ch->recv_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (coro) {
            coro->wait_node.freed_by_destroy = true;
        }
    }

    free(ch);
}