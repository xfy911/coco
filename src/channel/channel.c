/**
 * channel.c - Channel 实现
 *
 * 支持有缓冲（环形缓冲区）和无缓冲（同步传递）channel。
 */

#include "../coco_internal.h"
#include <stdlib.h>
#include <string.h>

/* 外部全局变量（在 coro.c 中定义） */
extern coco_sched_t *g_current_sched;
extern coco_coro_t *g_current_coro;

/* 等待队列节点 */
typedef struct wait_node {
    coco_coro_t *coro;
    void *value;
    struct wait_node *next;
} wait_node_t;

/* Channel 结构 */
struct coco_channel {
    size_t capacity;          /* 缓冲区大小（0 = 无缓冲） */
    int closed;               /* 是否已关闭 */

    /* 有缓冲 channel: 环形缓冲区 */
    void **buffer;
    size_t head;              /* 读位置 */
    size_t tail;              /* 写位置 */
    size_t count;             /* 当前元素数 */

    /* 等待队列 */
    wait_node_t *send_wait_head;  /* 发送等待队列 */
    wait_node_t *send_wait_tail;
    wait_node_t *recv_wait_head;  /* 接收等待队列 */
    wait_node_t *recv_wait_tail;
};

/* 创建等待节点 */
static wait_node_t *create_wait_node(coco_coro_t *coro, void *value) {
    wait_node_t *node = calloc(1, sizeof(wait_node_t));
    if (!node) {
        return NULL;
    }
    node->coro = coro;
    node->value = value;
    return node;
}

/* 添加到等待队列尾部 */
static void enqueue_wait(wait_node_t **head, wait_node_t **tail, wait_node_t *node) {
    node->next = NULL;
    if (*tail) {
        (*tail)->next = node;
    } else {
        *head = node;
    }
    *tail = node;
}

/* 从等待队列头部取出 */
static wait_node_t *dequeue_wait(wait_node_t **head, wait_node_t **tail) {
    wait_node_t *node = *head;
    if (!node) {
        return NULL;
    }
    *head = node->next;
    if (!*head) {
        *tail = NULL;
    }
    return node;
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
    wait_node_t *recv_waiter = dequeue_wait(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        /* 直接传递给接收者 */
        recv_waiter->value = value;
        enqueue_ready(sched, recv_waiter->coro);
        /* 不释放 recv_waiter：接收者恢复后会自己释放 */
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
    wait_node_t *node = create_wait_node(coro, value);
    if (!node) {
        return COCO_ERROR_NOMEM;
    }

    enqueue_wait(&ch->send_wait_head, &ch->send_wait_tail, node);
    coro->state = COCO_STATE_WAITING;
    coco_yield();

    /* 恢复后释放自己的等待节点 */
    free(node);

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
        wait_node_t *send_waiter = dequeue_wait(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            ch->buffer[ch->tail] = send_waiter->value;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            enqueue_ready(sched, send_waiter->coro);
            /* 不释放 send_waiter：发送者恢复后会自己释放 */
        }

        return COCO_OK;
    }

    /* 无缓冲 channel: 检查是否有发送者等待 */
    wait_node_t *send_waiter = dequeue_wait(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->value;
        enqueue_ready(sched, send_waiter->coro);
        /* 不释放 send_waiter：发送者恢复后会自己释放 */
        return COCO_OK;
    }

    /* 阻塞等待 */
    wait_node_t *node = create_wait_node(coro, NULL);
    if (!node) {
        return COCO_ERROR_NOMEM;
    }

    enqueue_wait(&ch->recv_wait_head, &ch->recv_wait_tail, node);
    coro->state = COCO_STATE_WAITING;
    coco_yield();

    /* 恢复后获取值 */
    if (ch->closed && !node->value) {
        free(node);
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    *value = node->value;
    free(node);

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
        wait_node_t *node = dequeue_wait(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (sched && node->coro) {
            enqueue_ready(sched, node->coro);
        }
        free(node);
    }

    /* 唤醒所有等待的发送者 */
    while (ch->send_wait_head) {
        wait_node_t *node = dequeue_wait(&ch->send_wait_head, &ch->send_wait_tail);
        if (sched && node->coro) {
            enqueue_ready(sched, node->coro);
        }
        free(node);
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

    /* 清理等待队列 */
    while (ch->send_wait_head) {
        wait_node_t *node = dequeue_wait(&ch->send_wait_head, &ch->send_wait_tail);
        free(node);
    }
    while (ch->recv_wait_head) {
        wait_node_t *node = dequeue_wait(&ch->recv_wait_head, &ch->recv_wait_tail);
        free(node);
    }

    free(ch);
}