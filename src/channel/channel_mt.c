/**
 * channel_mt.c - Channel 多线程实现 (Phase 1, US-007)
 *
 * 互斥锁保护的 channel，支持多线程场景。
 */

#include "channel_common.h"
#include "channel_mt.h"
#include "../sched/sched.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* 外部全局变量 */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/**
 * coco_channel_mt_create - 创建多线程 channel
 */
coco_channel_mt_t *coco_channel_mt_create(size_t capacity) {
    coco_channel_mt_t *ch = calloc(1, sizeof(coco_channel_mt_t));
    if (!ch) {
        return NULL;
    }

    ch->capacity = capacity;
    ch->closed = 0;

    if (pthread_mutex_init(&ch->lock, NULL) != 0) {
        free(ch);
        return NULL;
    }

    if (pthread_cond_init(&ch->send_cond, NULL) != 0) {
        pthread_mutex_destroy(&ch->lock);
        free(ch);
        return NULL;
    }

    if (pthread_cond_init(&ch->recv_cond, NULL) != 0) {
        pthread_cond_destroy(&ch->send_cond);
        pthread_mutex_destroy(&ch->lock);
        free(ch);
        return NULL;
    }

    if (capacity > 0) {
        ch->buffer = calloc(capacity, sizeof(void*));
        if (!ch->buffer) {
            pthread_cond_destroy(&ch->recv_cond);
            pthread_cond_destroy(&ch->send_cond);
            pthread_mutex_destroy(&ch->lock);
            free(ch);
            return NULL;
        }
    }

    return ch;
}

/**
 * coco_channel_mt_destroy - 销毁多线程 channel
 */
void coco_channel_mt_destroy(coco_channel_mt_t *ch) {
    if (!ch) {
        return;
    }

    pthread_mutex_lock(&ch->lock);

    /* 唤醒所有等待者 */
    while (ch->recv_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (coro) {
            coro->wait_node.value = NULL;
        }
    }

    while (ch->send_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (coro) {
            coro->wait_node.value = NULL;
        }
    }

    if (ch->buffer) {
        free(ch->buffer);
    }

    pthread_mutex_unlock(&ch->lock);

    pthread_cond_destroy(&ch->recv_cond);
    pthread_cond_destroy(&ch->send_cond);
    pthread_mutex_destroy(&ch->lock);
    free(ch);
}

/**
 * coco_channel_mt_send - 协程发送 (阻塞)
 */
int coco_channel_mt_send(coco_channel_mt_t *ch, void *value) {
    if (!ch) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 检查是否有接收者在等待 */
    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        recv_waiter->wait_node.value = value;
        pthread_mutex_unlock(&ch->lock);
        /* 唤醒接收者 */
        schedule_ready(recv_waiter);
        return COCO_OK;
    }

    /* 有缓冲 channel: 尝试放入缓冲区 */
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = value;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        pthread_cond_signal(&ch->recv_cond);
        pthread_mutex_unlock(&ch->lock);
        return COCO_OK;
    }

    /* 无缓冲或缓冲区满: 阻塞等待 */
    coco_coro_t *coro = g_current_coro;
    if (!coro || coro->wait_node.in_use) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR;
    }

    coro->wait_node.value = value;
    enqueue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail, coro, ch);
    coro->state = COCO_STATE_WAITING;
    pthread_mutex_unlock(&ch->lock);

    coco_yield();

    /* 恢复后检查 */
    pthread_mutex_lock(&ch->lock);
    if (coro->cancelled) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CANCELLED;
    }
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CHANNEL_CLOSED;
    }
    pthread_mutex_unlock(&ch->lock);

    return COCO_OK;
}

/**
 * coco_channel_mt_recv - 协程接收 (阻塞)
 */
int coco_channel_mt_recv(coco_channel_mt_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&ch->lock);

    if (ch->closed && ch->count == 0 && !ch->send_wait_head) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CHANNEL_CLOSED;
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
            pthread_mutex_unlock(&ch->lock);
            schedule_ready(send_waiter);
            return COCO_OK;
        }

        pthread_cond_signal(&ch->send_cond);
        pthread_mutex_unlock(&ch->lock);
        return COCO_OK;
    }

    /* 无缓冲 channel: 检查是否有发送者等待 */
    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->wait_node.value;
        pthread_mutex_unlock(&ch->lock);
        schedule_ready(send_waiter);
        return COCO_OK;
    }

    /* 阻塞等待 */
    coco_coro_t *coro = g_current_coro;
    if (!coro || coro->wait_node.in_use) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR;
    }

    coro->wait_node.value = NULL;
    enqueue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail, coro, ch);
    coro->state = COCO_STATE_WAITING;
    pthread_mutex_unlock(&ch->lock);

    coco_yield();

    /* 恢复后获取值 */
    pthread_mutex_lock(&ch->lock);
    if (coro->cancelled) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CANCELLED;
    }
    if (ch->closed && !coro->wait_node.value) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CHANNEL_CLOSED;
    }
    *value = coro->wait_node.value;
    pthread_mutex_unlock(&ch->lock);

    return COCO_OK;
}

/**
 * coco_channel_mt_send_thread - 线程发送 (阻塞)
 */
int coco_channel_mt_send_thread(coco_channel_mt_t *ch, void *value) {
    if (!ch) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&ch->lock);

    while (!ch->closed) {
        /* 检查是否有接收者在等待 */
        coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (recv_waiter) {
            recv_waiter->wait_node.value = value;
            pthread_mutex_unlock(&ch->lock);
            schedule_ready(recv_waiter);
            return COCO_OK;
        }

        /* 有缓冲 channel: 尝试放入缓冲区 */
        if (ch->capacity > 0 && ch->count < ch->capacity) {
            ch->buffer[ch->tail] = value;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            pthread_cond_signal(&ch->recv_cond);
            pthread_mutex_unlock(&ch->lock);
            return COCO_OK;
        }

        /* 等待空间 */
        pthread_cond_wait(&ch->send_cond, &ch->lock);
    }

    pthread_mutex_unlock(&ch->lock);
    return COCO_ERROR_CHANNEL_CLOSED;
}

/**
 * coco_channel_mt_recv_thread - 线程接收 (阻塞)
 */
int coco_channel_mt_recv_thread(coco_channel_mt_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&ch->lock);

    while (!ch->closed || ch->count > 0 || ch->send_wait_head) {
        /* 有缓冲 channel: 尝试从缓冲区取 */
        if (ch->capacity > 0 && ch->count > 0) {
            *value = ch->buffer[ch->head];
            ch->head = (ch->head + 1) % ch->capacity;
            ch->count--;

            coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
            if (send_waiter) {
                ch->buffer[ch->tail] = send_waiter->wait_node.value;
                ch->tail = (ch->tail + 1) % ch->capacity;
                ch->count++;
                pthread_mutex_unlock(&ch->lock);
                schedule_ready(send_waiter);
                return COCO_OK;
            }

            pthread_cond_signal(&ch->send_cond);
            pthread_mutex_unlock(&ch->lock);
            return COCO_OK;
        }

        /* 无缓冲 channel: 检查是否有发送者等待 */
        coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            *value = send_waiter->wait_node.value;
            pthread_mutex_unlock(&ch->lock);
            schedule_ready(send_waiter);
            return COCO_OK;
        }

        /* 等待数据 */
        pthread_cond_wait(&ch->recv_cond, &ch->lock);
    }

    pthread_mutex_unlock(&ch->lock);
    return COCO_ERROR_CHANNEL_CLOSED;
}

/**
 * coco_channel_mt_try_send - 非阻塞发送
 */
int coco_channel_mt_try_send(coco_channel_mt_t *ch, void *value) {
    if (!ch) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 有空间则发送 */
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = value;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        pthread_cond_signal(&ch->recv_cond);
        pthread_mutex_unlock(&ch->lock);
        return COCO_OK;
    }

    /* 无缓冲 channel: 检查接收者 */
    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        recv_waiter->wait_node.value = value;
        pthread_mutex_unlock(&ch->lock);
        schedule_ready(recv_waiter);
        return COCO_OK;
    }

    pthread_mutex_unlock(&ch->lock);
    return COCO_ERROR_WOULD_BLOCK;
}

/**
 * coco_channel_mt_try_recv - 非阻塞接收
 */
int coco_channel_mt_try_recv(coco_channel_mt_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&ch->lock);

    /* 有数据则接收 */
    if (ch->capacity > 0 && ch->count > 0) {
        *value = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        pthread_cond_signal(&ch->send_cond);
        pthread_mutex_unlock(&ch->lock);
        return COCO_OK;
    }

    /* 无缓冲 channel: 检查发送者 */
    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->wait_node.value;
        pthread_mutex_unlock(&ch->lock);
        schedule_ready(send_waiter);
        return COCO_OK;
    }

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    pthread_mutex_unlock(&ch->lock);
    return COCO_ERROR_WOULD_BLOCK;
}

/**
 * coco_channel_mt_close - 关闭 channel
 */
void coco_channel_mt_close(coco_channel_mt_t *ch) {
    if (!ch) {
        return;
    }

    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return;
    }

    ch->closed = 1;

    /* 唤醒所有等待者 */
    while (ch->recv_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (coro) {
            coro->wait_node.value = NULL;
            schedule_ready(coro);
        }
    }

    while (ch->send_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (coro) {
            coro->wait_node.value = NULL;
            schedule_ready(coro);
        }
    }

    /* 唤醒所有阻塞的线程 */
    pthread_cond_broadcast(&ch->send_cond);
    pthread_cond_broadcast(&ch->recv_cond);

    pthread_mutex_unlock(&ch->lock);
}

/**
 * coco_channel_mt_len - 获取 channel 长度
 */
size_t coco_channel_mt_len(coco_channel_mt_t *ch) {
    if (!ch) {
        return 0;
    }

    pthread_mutex_lock(&ch->lock);
    size_t len = ch->count;
    pthread_mutex_unlock(&ch->lock);

    return len;
}

/**
 * coco_channel_mt_is_closed - 检查 channel 是否关闭
 */
bool coco_channel_mt_is_closed(coco_channel_mt_t *ch) {
    if (!ch) {
        return true;
    }

    pthread_mutex_lock(&ch->lock);
    bool closed = ch->closed != 0;
    pthread_mutex_unlock(&ch->lock);

    return closed;
}
