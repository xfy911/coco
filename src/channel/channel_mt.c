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

/* === 无锁 MPMC Ring Buffer 实现 === */

static int mpmc_ring_enqueue(mpmc_ring_t *ring, void *value, uint32_t capacity) {
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);

    if ((uint32_t)(tail - head) >= capacity) {
        return -1; /* full */
    }

    uint32_t new_tail = tail + 1;
    if (!atomic_compare_exchange_strong_explicit(
            &ring->tail, &tail, new_tail,
            memory_order_release, memory_order_relaxed)) {
        return -1; /* CAS failed */
    }

    atomic_store_explicit(&ring->buffer[tail % capacity], value, memory_order_release);
    return 0;
}

static int mpmc_ring_dequeue(mpmc_ring_t *ring, void **value, uint32_t capacity) {
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    if (head == tail) {
        return -1; /* empty */
    }

    uint32_t new_head = head + 1;
    if (!atomic_compare_exchange_strong_explicit(
            &ring->head, &head, new_head,
            memory_order_release, memory_order_relaxed)) {
        return -1; /* CAS failed */
    }

    *value = atomic_load_explicit(&ring->buffer[head % capacity], memory_order_acquire);
    return 0;
}

/* mutex 保护下的 mpmc_ring 操作（不需要 CAS） */
static int mpmc_ring_enqueue_locked(mpmc_ring_t *ring, void *value, uint32_t capacity) {
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    if ((uint32_t)(tail - head) >= capacity) {
        return -1;
    }
    atomic_store_explicit(&ring->buffer[tail % capacity], value, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
    return 0;
}

static int mpmc_ring_dequeue_locked(mpmc_ring_t *ring, void **value, uint32_t capacity) {
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    if (head == tail) {
        return -1;
    }
    *value = atomic_load_explicit(&ring->buffer[head % capacity], memory_order_relaxed);
    atomic_store_explicit(&ring->head, head + 1, memory_order_release);
    return 0;
}
/* 获取 mpmc_ring 当前元素数量 */
static uint32_t mpmc_ring_count(mpmc_ring_t *ring) {
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    return tail - head;
}


/* 从 select 等待队列头部取出 select_node */
static coco_select_node_t *dequeue_select_node_mt(coco_select_node_t **head,
                                                   coco_select_node_t **tail) {
    coco_select_node_t *node = *head;
    if (!node) {
        return NULL;
    }
    *head = node->next;
    if (!*head) {
        *tail = NULL;
    }
    node->next = NULL;
    node->registered = 0;
    return node;
}

/* 前向声明：select 辅助函数在文件后面定义 */
static void select_dequeue_all_mt(coco_coro_t *coro);
static void cancel_select_timer_mt(coco_coro_t *coro);

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
        /* 分配无锁 MPMC Ring Buffer */
        ch->mpmc_ring = calloc(1, sizeof(mpmc_ring_t) + capacity * sizeof(void*));
        if (!ch->mpmc_ring) {
            pthread_cond_destroy(&ch->recv_cond);
            pthread_cond_destroy(&ch->send_cond);
            pthread_mutex_destroy(&ch->lock);
            free(ch);
            return NULL;
        }
        atomic_init(&ch->mpmc_ring->head, 0);
        atomic_init(&ch->mpmc_ring->tail, 0);
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

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    /* 清理 send_select_head */
    while (ch->send_select_head) {
        coco_select_node_t *node = dequeue_select_node_mt(&ch->send_select_head, &ch->send_select_tail);
        if (node && node->coro) {
            node->chan = NULL;
            coco_coro_t *sel_coro = node->coro;
            if (sel_coro->select_ready_index == -1) {
                sel_coro->select_ready_index = node->case_index;
                select_dequeue_all_mt(sel_coro);
                cancel_select_timer_mt(sel_coro);
                schedule_ready(sel_coro);
            }
        }
    }

    /* 清理 recv_select_head */
    while (ch->recv_select_head) {
        coco_select_node_t *node = dequeue_select_node_mt(&ch->recv_select_head, &ch->recv_select_tail);
        if (node && node->coro) {
            node->chan = NULL;
            coco_coro_t *sel_coro = node->coro;
            if (sel_coro->select_ready_index == -1) {
                sel_coro->select_ready_index = node->case_index;
                select_dequeue_all_mt(sel_coro);
                cancel_select_timer_mt(sel_coro);
                schedule_ready(sel_coro);
            }
        }
    }

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

    if (ch->mpmc_ring) {
        free(ch->mpmc_ring);
    }

    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

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

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 检查是否有接收者在等待 */
    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        recv_waiter->wait_node.value = value;
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        /* 唤醒接收者 */
        schedule_ready(recv_waiter);
        return COCO_OK;
    }

    /* 有缓冲 channel: 尝试放入无锁 ring buffer（可能在拿锁期间有空位了） */
    if (ch->mpmc_ring && mpmc_ring_enqueue(ch->mpmc_ring, value, ch->capacity) == 0) {
        pthread_cond_signal(&ch->recv_cond);
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_OK;
    }

    /* 有缓冲 channel: 尝试放入 mutex 保护的缓冲区 */
    if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) < ch->capacity) {
        mpmc_ring_enqueue_locked(ch->mpmc_ring, value, ch->capacity);
        pthread_cond_signal(&ch->recv_cond);
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_OK;
    }

    /* 无缓冲或缓冲区满: 阻塞等待 */
    coco_coro_t *coro = g_current_coro;
    if (!coro || coro->wait_node.in_use) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR;
    }

    coro->wait_node.value = value;
    enqueue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail, coro, ch);
    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

    coco_yield();

    /* 恢复后检查 */
    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);
    if (coro->cancelled) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CANCELLED;
    }
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }
    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

    return COCO_OK;
}

/**
 * coco_channel_mt_recv - 协程接收 (阻塞)
 */
int coco_channel_mt_recv(coco_channel_mt_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    if (ch->closed && mpmc_ring_count(ch->mpmc_ring) == 0 && !ch->send_wait_head) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 有缓冲 channel: 尝试从 mutex 保护的缓冲区取 */
    if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) > 0) {
        mpmc_ring_dequeue_locked(ch->mpmc_ring, value, ch->capacity);
        /* 缓冲区有空间，唤醒发送者 */
        coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            mpmc_ring_enqueue_locked(ch->mpmc_ring, send_waiter->wait_node.value, ch->capacity);
            pthread_mutex_unlock(&ch->lock);
            coco_preempt_unblock_signal();
            schedule_ready(send_waiter);
            return COCO_OK;
        }

        pthread_cond_signal(&ch->send_cond);
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_OK;
    }

    /* 无缓冲 channel: 检查是否有发送者等待 */
    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->wait_node.value;
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        schedule_ready(send_waiter);
        return COCO_OK;
    }

    /* 阻塞等待 */
    coco_coro_t *coro = g_current_coro;
    if (!coro || coro->wait_node.in_use) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR;
    }

    coro->wait_node.value = NULL;
    enqueue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail, coro, ch);
    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

    coco_yield();

    /* 恢复后获取值 */
    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);
    if (coro->cancelled) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CANCELLED;
    }
    if (ch->closed && !coro->wait_node.value) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }
    *value = coro->wait_node.value;
    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

    return COCO_OK;
}

/**
 * coco_channel_mt_send_thread - 线程发送 (阻塞)
 */
int coco_channel_mt_send_thread(coco_channel_mt_t *ch, void *value) {
    if (!ch) {
        return COCO_ERROR;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    while (!ch->closed) {
        /* 检查是否有接收者在等待 */
        coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (recv_waiter) {
            recv_waiter->wait_node.value = value;
            pthread_mutex_unlock(&ch->lock);
            coco_preempt_unblock_signal();
            schedule_ready(recv_waiter);
            return COCO_OK;
        }

        /* 有缓冲 channel: 尝试放入缓冲区 */
        if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) < ch->capacity) {
            mpmc_ring_enqueue_locked(ch->mpmc_ring, value, ch->capacity);
            pthread_cond_signal(&ch->recv_cond);
            pthread_mutex_unlock(&ch->lock);
            coco_preempt_unblock_signal();
            return COCO_OK;
        }

        /* 等待空间 */
        pthread_cond_wait(&ch->send_cond, &ch->lock);
    }

    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();
    return COCO_ERROR_CHANNEL_CLOSED;
}

/**
 * coco_channel_mt_recv_thread - 线程接收 (阻塞)
 */
int coco_channel_mt_recv_thread(coco_channel_mt_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    while (!ch->closed || mpmc_ring_count(ch->mpmc_ring) > 0 || ch->send_wait_head) {
        /* 有缓冲 channel: 尝试从缓冲区取 */
        if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) > 0) {
            mpmc_ring_dequeue_locked(ch->mpmc_ring, value, ch->capacity);
            coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
            if (send_waiter) {
                mpmc_ring_enqueue_locked(ch->mpmc_ring, send_waiter->wait_node.value, ch->capacity);
                pthread_mutex_unlock(&ch->lock);
                coco_preempt_unblock_signal();
                schedule_ready(send_waiter);
                return COCO_OK;
            }

            pthread_cond_signal(&ch->send_cond);
            pthread_mutex_unlock(&ch->lock);
            coco_preempt_unblock_signal();
            return COCO_OK;
        }

        /* 无缓冲 channel: 检查是否有发送者等待 */
        coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            *value = send_waiter->wait_node.value;
            pthread_mutex_unlock(&ch->lock);
            coco_preempt_unblock_signal();
            schedule_ready(send_waiter);
            return COCO_OK;
        }

        /* 等待数据 */
        pthread_cond_wait(&ch->recv_cond, &ch->lock);
    }

    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();
    return COCO_ERROR_CHANNEL_CLOSED;
}

/**
 * coco_channel_mt_try_send - 非阻塞发送
 */
int coco_channel_mt_try_send(coco_channel_mt_t *ch, void *value) {
    if (!ch) {
        return COCO_ERROR;
    }

    /* 快速路径: 尝试无锁 MPMC ring buffer */
    if (ch->mpmc_ring && mpmc_ring_enqueue(ch->mpmc_ring, value, ch->capacity) == 0) {
        pthread_cond_signal(&ch->recv_cond);
        return COCO_OK;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 有空间则发送 */
    if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) < ch->capacity) {
        mpmc_ring_enqueue_locked(ch->mpmc_ring, value, ch->capacity);
        pthread_cond_signal(&ch->recv_cond);
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_OK;
    }

    /* 无缓冲 channel: 检查接收者 */
    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        recv_waiter->wait_node.value = value;
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        schedule_ready(recv_waiter);
        return COCO_OK;
    }

    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();
    return COCO_ERROR_WOULD_BLOCK;
}

/**
 * coco_channel_mt_try_recv - 非阻塞接收
 */
int coco_channel_mt_try_recv(coco_channel_mt_t *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    /* 快速路径: 尝试无锁 MPMC ring buffer */
    if (ch->mpmc_ring && mpmc_ring_dequeue(ch->mpmc_ring, value, ch->capacity) == 0) {
        pthread_cond_signal(&ch->send_cond);
        return COCO_OK;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    /* 有数据则接收 */
    if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) > 0) {
        mpmc_ring_dequeue_locked(ch->mpmc_ring, value, ch->capacity);
        pthread_cond_signal(&ch->send_cond);
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_OK;
    }

    /* 无缓冲 channel: 检查发送者 */
    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->wait_node.value;
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        schedule_ready(send_waiter);
        return COCO_OK;
    }

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();
    return COCO_ERROR_WOULD_BLOCK;
}

/**
 * coco_channel_mt_close - 关闭 channel
 */
void coco_channel_mt_close(coco_channel_mt_t *ch) {
    if (!ch) {
        return;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        coco_preempt_unblock_signal();
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
    coco_preempt_unblock_signal();
}

/**
 * coco_channel_mt_len - 获取 channel 长度
 */
size_t coco_channel_mt_len(coco_channel_mt_t *ch) {
    if (!ch) {
        return 0;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);
    size_t len = ch->mpmc_ring ? mpmc_ring_count(ch->mpmc_ring) : 0;
    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

    return len;
}

/**
 * coco_channel_mt_is_closed - 检查 channel 是否关闭
 */
bool coco_channel_mt_is_closed(coco_channel_mt_t *ch) {
    if (!ch) {
        return true;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->lock);
    bool closed = ch->closed != 0;
    pthread_mutex_unlock(&ch->lock);
    coco_preempt_unblock_signal();

    return closed;
}

/* === Select 辅助函数 === */

/* 添加 select_node 到 channel_mt 等待队列尾部 */
static void enqueue_select_node_mt(coco_select_node_t **head, coco_select_node_t **tail,
                                   coco_select_node_t *node) {
    node->next = NULL;
    if (*tail) {
        (*tail)->next = node;
    } else {
        *head = node;
    }
    *tail = node;
    node->registered = 1;
}

/* 从等待队列中移除指定 select_node */
static void remove_select_node_mt(coco_select_node_t **head, coco_select_node_t **tail,
                                  coco_select_node_t *node) {
    if (*head == node) {
        *head = node->next;
        if (!*head) {
            *tail = NULL;
        }
        node->next = NULL;
        node->registered = 0;
        return;
    }
    coco_select_node_t *prev = *head;
    while (prev && prev->next != node) {
        prev = prev->next;
    }
    if (prev) {
        prev->next = node->next;
        if (*tail == node) {
            *tail = prev;
        }
        node->next = NULL;
        node->registered = 0;
    }
}

/* 从所有 select 注册的 channel 等待队列中移除协程 */
static void select_dequeue_all_mt(coco_coro_t *coro) {
    coco_select_node_t *nodes = coro->select_nodes;
    coco_preempt_block_signal();
    for (int i = 0; i < coro->select_case_count; i++) {
        if (nodes[i].registered) {
            coco_channel_mt_t *ch = (coco_channel_mt_t *)nodes[i].chan;
            if (ch) {
                pthread_mutex_lock(&ch->lock);
                if (nodes[i].is_send) {
                    remove_select_node_mt(&ch->send_select_head, &ch->send_select_tail, &nodes[i]);
                } else {
                    remove_select_node_mt(&ch->recv_select_head, &ch->recv_select_tail, &nodes[i]);
                }
                pthread_mutex_unlock(&ch->lock);
            }
            nodes[i].registered = 0;
        }
    }
    coco_preempt_unblock_signal();
}

/* select 超时回调参数 */
typedef struct {
    coco_sched_t *sched;
    coco_coro_t *coro;
    _Atomic bool callback_executed;
    _Atomic bool callback_done;
} select_timeout_arg_mt_t;

/* select 超时回调 */
static void select_timeout_cb_mt(void *arg) {
    select_timeout_arg_mt_t *ta = (select_timeout_arg_mt_t *)arg;
    if (atomic_exchange(&ta->callback_executed, true)) {
        return;
    }
    coco_coro_t *coro = ta->coro;
    if (coro->select_nodes && coro->select_ready_index == -1) {
        coro->select_ready_index = COCO_SELECT_TIMEOUT;
        select_dequeue_all_mt(coro);
        coro->select_timer = NULL;
        enqueue_ready(ta->sched, coro);
    }
    atomic_store(&ta->callback_done, true);
    free(ta);
}

/* 取消 select 定时器 */
static void cancel_select_timer_mt(coco_coro_t *coro) {
    if (coro->select_timer) {
        select_timeout_arg_mt_t *ta = (select_timeout_arg_mt_t *)coro->select_timer->arg;
        if (atomic_exchange(&ta->callback_executed, true)) {
            while (!atomic_load(&ta->callback_done)) {
                /* spin wait */
            }
        } else {
            coco_timer_cancel(coro->select_timer);
            free(ta);
        }
        coro->select_timer = NULL;
    }
}

/**
 * coco_channel_mt_select - 多线程 channel select
 *
 * @param cases     select case 数组
 * @param ncases    case 数量
 * @param timeout_ms 超时时间（0 = 无超时）
 * @param has_default 是否有 default case
 * @return 就绪 case 的索引，COCO_SELECT_TIMEOUT，或 COCO_SELECT_DEFAULT
 */
int coco_channel_mt_select(coco_select_case_t *cases, int ncases,
                           uint64_t timeout_ms, int has_default) {
    if (!cases || ncases <= 0) {
        return COCO_ERROR_INVALID;
    }

    ENSURE_IN_CORO_RET(COCO_ERROR_INVALID);

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    /* Phase 1: Non-blocking check with trylock */
    for (int i = 0; i < ncases; i++) {
        coco_channel_mt_t *ch = (coco_channel_mt_t *)cases[i].chan;
        if (!ch) continue;

        if (cases[i].dir == COCO_SELECT_RECV) {
            if (pthread_mutex_trylock(&ch->lock) == 0) {
                if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) > 0) {
                    void *val; mpmc_ring_dequeue_locked(ch->mpmc_ring, &val, ch->capacity);
                    
                    
                    *(void **)cases[i].val = val;
                    cases[i].result = COCO_OK;
                    /* 尝试唤醒等待的发送者 */
                    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
                    if (send_waiter) {
                        mpmc_ring_enqueue_locked(ch->mpmc_ring, send_waiter->wait_node.value, ch->capacity);
                        pthread_mutex_unlock(&ch->lock);
                        schedule_ready(send_waiter);
                    } else {
                        pthread_cond_signal(&ch->send_cond);
                        pthread_mutex_unlock(&ch->lock);
                    }
                    return i;
                }
                if (ch->capacity == 0 && ch->send_wait_head) {
                    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
                    if (send_waiter) {
                        *(void **)cases[i].val = send_waiter->wait_node.value;
                        pthread_mutex_unlock(&ch->lock);
                        schedule_ready(send_waiter);
                        cases[i].result = COCO_OK;
                        return i;
                    }
                }
                pthread_mutex_unlock(&ch->lock);
            }
        } else {
            if (pthread_mutex_trylock(&ch->lock) == 0) {
                if (ch->closed) {
                    cases[i].result = COCO_ERROR_CHANNEL_CLOSED;
                    pthread_mutex_unlock(&ch->lock);
                    return i;
                }
                if (ch->capacity > 0 && mpmc_ring_count(ch->mpmc_ring) < ch->capacity) {
                    mpmc_ring_enqueue_locked(ch->mpmc_ring, cases[i].val, ch->capacity);
                    cases[i].result = COCO_OK;
                    /* 尝试唤醒等待的接收者 */
                    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
                    if (recv_waiter) {
                        mpmc_ring_dequeue_locked(ch->mpmc_ring, &recv_waiter->wait_node.value, ch->capacity);
                        
                        
                        pthread_mutex_unlock(&ch->lock);
                        schedule_ready(recv_waiter);
                    } else {
                        pthread_cond_signal(&ch->recv_cond);
                        pthread_mutex_unlock(&ch->lock);
                    }
                    return i;
                }
                if (ch->capacity == 0 && ch->recv_wait_head) {
                    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
                    if (recv_waiter) {
                        recv_waiter->wait_node.value = cases[i].val;
                        pthread_mutex_unlock(&ch->lock);
                        schedule_ready(recv_waiter);
                        cases[i].result = COCO_OK;
                        return i;
                    }
                }
                pthread_mutex_unlock(&ch->lock);
            }
        }
    }

    /* Default case */
    if (has_default) {
        return COCO_SELECT_DEFAULT;
    }

    /* Phase 2: Register on all channels */
    coco_select_node_t *nodes = malloc(sizeof(coco_select_node_t) * ncases);
    if (!nodes) {
        return COCO_ERROR_NOMEM;
    }

    if (coro->wait_node.in_use) {
        free(nodes);
        return COCO_ERROR;
    }

    /* 初始化节点 */
    for (int i = 0; i < ncases; i++) {
        coco_channel_mt_t *ch = (coco_channel_mt_t *)cases[i].chan;
        nodes[i].chan = (coco_channel_t *)ch;
        nodes[i].coro = coro;
        nodes[i].case_index = i;
        nodes[i].is_send = (cases[i].dir == COCO_SELECT_SEND);
        if (nodes[i].is_send) {
            nodes[i].send_val = cases[i].val;
        } else {
            nodes[i].recv_ptr = (void **)cases[i].val;
        }
        nodes[i].registered = 0;
        nodes[i].next = NULL;
    }

    /* 按 channel 地址排序获取锁，避免死锁 */
    /* 简单实现：逐个 trylock，失败则释放所有已获取的锁，重试 */
    int retry = 0;
    while (retry < 100) {
        int locked = 0;
        for (int i = 0; i < ncases; i++) {
            coco_channel_mt_t *ch = (coco_channel_mt_t *)nodes[i].chan;
            if (!ch) continue;
            if (pthread_mutex_trylock(&ch->lock) != 0) {
                /* 释放已获取的锁 */
                for (int j = 0; j < i; j++) {
                    coco_channel_mt_t *prev_ch = (coco_channel_mt_t *)nodes[j].chan;
                    if (prev_ch) pthread_mutex_unlock(&prev_ch->lock);
                }
                locked = 0;
                break;
            }
            locked++;
        }
        if (locked > 0 || ncases == 0) {
            /* 成功获取所有锁 */
            break;
        }
        /* 短暂退让后重试 */
        retry++;
        sched_yield();
    }
    if (retry >= 100) {
        /* 获取锁失败，回退到阻塞获取（按固定顺序） */
        for (int i = 0; i < ncases; i++) {
            coco_channel_mt_t *ch = (coco_channel_mt_t *)nodes[i].chan;
            if (ch) pthread_mutex_lock(&ch->lock);
        }
    }

    /* 注册到各 channel 的 select 队列 */
    for (int i = 0; i < ncases; i++) {
        coco_channel_mt_t *ch = (coco_channel_mt_t *)nodes[i].chan;
        if (!ch) continue;
        if (nodes[i].is_send) {
            if (!ch->closed) {
                enqueue_select_node_mt(&ch->send_select_head, &ch->send_select_tail, &nodes[i]);
            }
        } else {
            enqueue_select_node_mt(&ch->recv_select_head, &ch->recv_select_tail, &nodes[i]);
        }
    }

    /* 释放所有锁 */
    for (int i = 0; i < ncases; i++) {
        coco_channel_mt_t *ch = (coco_channel_mt_t *)nodes[i].chan;
        if (ch) pthread_mutex_unlock(&ch->lock);
    }

    coro->select_nodes = nodes;
    coro->select_case_count = ncases;
    coro->select_ready_index = -1;
    coro->select_timer = NULL;

    /* 设置超时定时器 */
    if (timeout_ms > 0) {
        select_timeout_arg_mt_t *ta = malloc(sizeof(select_timeout_arg_mt_t));
        if (ta) {
            ta->sched = sched;
            ta->coro = coro;
            atomic_init(&ta->callback_executed, false);
            atomic_init(&ta->callback_done, false);
            coro->select_timer = coco_timer_ex(sched, timeout_ms, select_timeout_cb_mt, ta);
            if (!coro->select_timer) {
                free(ta);
            }
        }
    }

    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
    coco_yield();

    /* Phase 3: Resume */
    int ready_index = coro->select_ready_index;

    if (coro->select_timer) {
        cancel_select_timer_mt(coro);
    }

    if (ready_index >= 0) {
        coco_select_node_t *node = &nodes[ready_index];
        coco_channel_mt_t *ch = (coco_channel_mt_t *)node->chan;
        if (node->is_send) {
            cases[ready_index].result = (ch && ch->closed) ? COCO_ERROR_CHANNEL_CLOSED : COCO_OK;
        } else {
            *node->recv_ptr = coro->wait_node.value;
            cases[ready_index].result = (coro->wait_node.value == NULL && ch && ch->closed)
                                        ? COCO_ERROR_CHANNEL_CLOSED : COCO_OK;
        }
    }

    coro->select_nodes = NULL;
    coro->select_case_count = 0;
    coro->select_ready_index = -1;
    free(nodes);

    return ready_index;
}
