/**
 * channel.c - Channel 实现
 *
 * 支持有缓冲（环形缓冲区）和无缓冲（同步传递）channel。
 * 使用嵌入式等待节点，避免动态内存分配。
 * 支持 Go 风格的 channel select（多路复用）。
 */

#include "../coco_internal.h"
#include "channel_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* 外部全局变量（TLS，在 coro.c 中定义） */


/* Channel 结构 */
struct coco_channel {
    size_t capacity;          /* 缓冲区大小（0 = 无缓冲） */
    _Atomic int closed;               /* 是否已关闭 (atomic for thread safety) */

    /* 引用计数和同步 */
    _Atomic uint32_t refcount;        /* 引用计数 */
    pthread_mutex_t wait_queue_lock;  /* 等待队列锁 */
    _Atomic bool destroying;          /* 正在销毁标志 */

    /* 有缓冲 channel: 环形缓冲区 */
    void **buffer;
    size_t head;              /* 读位置 */
    size_t tail;              /* 写位置 */
    size_t count;             /* 当前元素数 */

    /* 等待队列（普通协程，使用嵌入式 wait_node） */
    coco_coro_t *send_wait_head;
    coco_coro_t *send_wait_tail;
    coco_coro_t *recv_wait_head;
    coco_coro_t *recv_wait_tail;

    /* 等待队列（select 协程，使用 select_node 链表） */
    coco_select_node_t *send_select_head;
    coco_select_node_t *send_select_tail;
    coco_select_node_t *recv_select_head;
    coco_select_node_t *recv_select_tail;
};

/* === 引用计数函数 === */

/**
 * coco_channel_ref - 增加 channel 引用计数
 *
 * @param ch channel 指针
 */
void coco_channel_ref(coco_channel_t *ch) {
    if (ch) {
        atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
    }
}

/**
 * coco_channel_unref - 减少 channel 引用计数，计数为 0 时释放
 *
 * @param ch channel 指针
 * @return true 如果 channel 已释放，false 否则
 */
bool coco_channel_unref(coco_channel_t *ch) {
    if (!ch) {
        return false;
    }
    if (atomic_fetch_sub_explicit(&ch->refcount, 1, memory_order_acq_rel) == 1) {
        if (ch->buffer) {
            free(ch->buffer);
        }
        pthread_mutex_destroy(&ch->wait_queue_lock);
        free(ch);
        return true;
    }
    return false;
}

/**
 * coco_channel_remove_waiter - 从 channel 等待队列中移除指定协程
 *
 * @param ch channel 指针
 * @param coro 要移除的协程
 *
 * 从发送队列或接收队列中移除协程，更新 tail 指针。
 * 调用者必须持有 wait_queue_lock。
 */
void coco_channel_remove_waiter(coco_channel_t *ch, coco_coro_t *coro) {
    /* 尝试从发送队列移除 */
    if (ch->send_wait_head == coro) {
        ch->send_wait_head = coro->wait_node.next_waiter;
        if (!ch->send_wait_head) {
            ch->send_wait_tail = NULL;
        }
    } else {
        coco_coro_t *prev = ch->send_wait_head;
        while (prev && prev->wait_node.next_waiter != coro) {
            prev = prev->wait_node.next_waiter;
        }
        if (prev) {
            prev->wait_node.next_waiter = coro->wait_node.next_waiter;
            if (ch->send_wait_tail == coro) {
                ch->send_wait_tail = prev;
            }
        } else {
            /* 不在发送队列，尝试从接收队列移除 */
            if (ch->recv_wait_head == coro) {
                ch->recv_wait_head = coro->wait_node.next_waiter;
                if (!ch->recv_wait_head) {
                    ch->recv_wait_tail = NULL;
                }
            } else {
                prev = ch->recv_wait_head;
                while (prev && prev->wait_node.next_waiter != coro) {
                    prev = prev->wait_node.next_waiter;
                }
                if (prev) {
                    prev->wait_node.next_waiter = coro->wait_node.next_waiter;
                    if (ch->recv_wait_tail == coro) {
                        ch->recv_wait_tail = prev;
                    }
                }
            }
        }
    }
    coro->wait_node.in_use = false;
    coro->wait_node.next_waiter = NULL;
    coro->wait_node.channel = NULL;
}

/**
 * coco_channel_cancel_cleanup - 取消时清理 channel 等待
 *
 * @param ch channel 指针
 * @param coro 要取消的协程
 *
 * 持锁从等待队列移除协程并减少引用计数。
 */
void coco_channel_cancel_cleanup(coco_channel_t *ch, coco_coro_t *coro) {
    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->wait_queue_lock);
    if (coro->wait_node.in_use && coro->wait_node.channel == ch) {
        coco_channel_remove_waiter(ch, coro);
        coco_channel_unref(ch);
    }
    pthread_mutex_unlock(&ch->wait_queue_lock);
    coco_preempt_unblock_signal();
}

/* === Select 协程等待队列操作 === */

/* 添加 select_node 到等待队列尾部 */
static void enqueue_select_node(coco_select_node_t **head, coco_select_node_t **tail,
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

/* 从等待队列头部取出 select_node */
static coco_select_node_t *dequeue_select_node(coco_select_node_t **head,
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

/* 从等待队列中移除指定 select_node（任意位置） */
static void remove_select_node(coco_select_node_t **head, coco_select_node_t **tail,
                               coco_select_node_t *node) {
    if (*head == node) {
        dequeue_select_node(head, tail);
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

/* === Select 辅助函数 === */

/* Argument struct for select timeout callback (carries sched + coro) */
typedef struct {
    coco_sched_t *sched;
    coco_coro_t *coro;
    _Atomic bool callback_executed;
    _Atomic bool callback_done;
} select_timeout_arg_t;

/* 从所有 select 注册的 channel 等待队列中移除协程 */
static void select_dequeue_all(coco_coro_t *coro) {
    coco_select_node_t *nodes = coro->select_nodes;
    for (int i = 0; i < coro->select_case_count; i++) {
        if (nodes[i].registered) {
            coco_channel_t *ch = nodes[i].chan;
            if (ch) {
                if (nodes[i].is_send) {
                    remove_select_node(&ch->send_select_head, &ch->send_select_tail, &nodes[i]);
                } else {
                    remove_select_node(&ch->recv_select_head, &ch->recv_select_tail, &nodes[i]);
                }
            }
            nodes[i].registered = 0;
        }
    }
}

/* Cancel a select timer and free its timeout arg struct */
static void cancel_select_timer(coco_coro_t *coro) {
    if (coro->select_timer) {
        select_timeout_arg_t *ta = (select_timeout_arg_t *)coro->select_timer->arg;
        /* Try to claim execution right - if callback already started, wait for it */
        if (atomic_exchange(&ta->callback_executed, true)) {
            /* Callback already running, wait for completion */
            while (!atomic_load(&ta->callback_done)) {
                /* Spin wait - callback should complete quickly */
            }
        } else {
            /* We got execution right, cancel timer and free */
            coco_timer_cancel(coro->select_timer);
            free(ta);
        }
        coro->select_timer = NULL;
    }
}

/* Public wrapper for use by coro.c (coco_destroy select cleanup) */
void coco_select_cleanup(coco_coro_t *coro) {
    if (coro && coro->select_nodes) {
        select_dequeue_all(coro);
        cancel_select_timer(coro);
        free(coro->select_nodes);
        coro->select_nodes = NULL;
        coro->select_case_count = 0;
        coro->select_ready_index = -1;
    }
}

/* select 超时回调 */
static void select_timeout_cb(void *arg) {
    select_timeout_arg_t *ta = (select_timeout_arg_t *)arg;
    /* Try to claim execution right - if cancel already claimed, return */
    if (atomic_exchange(&ta->callback_executed, true)) {
        /* Cancel already claimed execution, do nothing */
        return;
    }
    coco_coro_t *coro = ta->coro;
    coco_sched_t *sched = ta->sched;
    if (coro->select_nodes && coro->select_ready_index == -1) {
        coro->select_ready_index = COCO_SELECT_TIMEOUT;
        select_dequeue_all(coro);
        coro->select_timer = NULL;
        enqueue_ready(sched, coro);
    }
    atomic_store(&ta->callback_done, true);
    free(ta);
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
    atomic_init(&ch->closed, 0);

    /* 初始化引用计数和同步字段 */
    atomic_init(&ch->refcount, 1);
    atomic_init(&ch->destroying, false);
    if (pthread_mutex_init(&ch->wait_queue_lock, NULL) != 0) {
        free(ch);
        return NULL;
    }

    if (capacity > 0) {
        /* 有缓冲 channel: 分配环形缓冲区 */
        ch->buffer = calloc(capacity, sizeof(void*));
        if (!ch->buffer) {
            pthread_mutex_destroy(&ch->wait_queue_lock);
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
    if (!ch || atomic_load_explicit(&ch->closed, memory_order_acquire)) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    ENSURE_IN_CORO_RET(COCO_ERROR_INVALID);

    /* 优先唤醒普通接收者 */
    coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
    if (recv_waiter) {
        recv_waiter->wait_node.value = value;
        enqueue_ready(sched, recv_waiter);
        return COCO_OK;
    }

    /* 然后唤醒 select 接收者 */
    coco_select_node_t *recv_node = dequeue_select_node(&ch->recv_select_head,
                                                        &ch->recv_select_tail);
    while (recv_node) {
        coco_coro_t *sel_coro = recv_node->coro;
        if (!sel_coro->select_nodes || sel_coro->select_ready_index != -1) {
            /* Race: select state already resolved, try next node */
            recv_node = dequeue_select_node(&ch->recv_select_head, &ch->recv_select_tail);
            continue;
        }

        sel_coro->wait_node.value = value;
        sel_coro->select_ready_index = recv_node->case_index;

        /* 从所有其他 channel 的 select 队列中移除 */
        select_dequeue_all(sel_coro);

        /* 取消 select 定时器 */
        cancel_select_timer(sel_coro);

        enqueue_ready(sched, sel_coro);
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

    /* 检查是否正在销毁 */
    if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 持锁增加引用计数并入队等待 */
    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->wait_queue_lock);
    if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
        pthread_mutex_unlock(&ch->wait_queue_lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }
    atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
    coro->wait_node.value = value;
    coro->wait_node.freed_by_destroy = false;
    enqueue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail, coro, ch);
    pthread_mutex_unlock(&ch->wait_queue_lock);
    coco_preempt_unblock_signal();

    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
    coco_yield();

    /* 减少引用计数 */
    coco_channel_unref(ch);

    /* 检查是否被取消 */
    if (coro->cancelled) {
        return COCO_ERROR_CANCELLED;
    }

    /* 恢复后检查 channel 是否关闭或被销毁 */
    if (coro->wait_node.freed_by_destroy || atomic_load_explicit(&ch->closed, memory_order_acquire)) {
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

    if (atomic_load_explicit(&ch->closed, memory_order_acquire) && ch->count == 0 && !ch->send_wait_head && !ch->send_select_head) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    ENSURE_IN_CORO_RET(COCO_ERROR_INVALID);

    /* 有缓冲 channel: 尝试从缓冲区取 */
    if (ch->capacity > 0 && ch->count > 0) {
        *value = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        /* 缓冲区有空间，尝试唤醒发送者 */
        coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            ch->buffer[ch->tail] = send_waiter->wait_node.value;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            enqueue_ready(sched, send_waiter);
        } else {
            /* 尝试唤醒 select 发送者 */
            coco_select_node_t *send_node = dequeue_select_node(&ch->send_select_head,
                                                                &ch->send_select_tail);
            while (send_node) {
                coco_coro_t *sel_coro = send_node->coro;
                if (!sel_coro->select_nodes || sel_coro->select_ready_index != -1) {
                    /* Race: select state already resolved, try next node */
                    send_node = dequeue_select_node(&ch->send_select_head,
                                                    &ch->send_select_tail);
                    continue;
                }

                ch->buffer[ch->tail] = send_node->send_val;
                ch->tail = (ch->tail + 1) % ch->capacity;
                ch->count++;

                sel_coro->select_ready_index = send_node->case_index;
                select_dequeue_all(sel_coro);
                cancel_select_timer(sel_coro);
                enqueue_ready(sched, sel_coro);
                break;
            }
        }

        return COCO_OK;
    }

    /* 无缓冲 channel: 优先检查普通发送者 */
    coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
    if (send_waiter) {
        *value = send_waiter->wait_node.value;
        enqueue_ready(sched, send_waiter);
        return COCO_OK;
    }

    /* 然后检查 select 发送者 */
    coco_select_node_t *send_node = dequeue_select_node(&ch->send_select_head,
                                                        &ch->send_select_tail);
    while (send_node) {
        coco_coro_t *sel_coro = send_node->coro;
        if (!sel_coro->select_nodes || sel_coro->select_ready_index != -1) {
            /* Race: select state already resolved, try next node */
            send_node = dequeue_select_node(&ch->send_select_head,
                                            &ch->send_select_tail);
            continue;
        }

        *value = send_node->send_val;
        sel_coro->select_ready_index = send_node->case_index;
        select_dequeue_all(sel_coro);
        cancel_select_timer(sel_coro);
        enqueue_ready(sched, sel_coro);
        return COCO_OK;
    }

    /* 阻塞等待 */
    if (coro->wait_node.in_use) {
        return COCO_ERROR;  /* 协程已在等待队列中 */
    }

    /* 检查是否正在销毁 */
    if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    /* 持锁增加引用计数并入队等待 */
    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->wait_queue_lock);
    if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
        pthread_mutex_unlock(&ch->wait_queue_lock);
        coco_preempt_unblock_signal();
        return COCO_ERROR_CHANNEL_CLOSED;
    }
    atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
    coro->wait_node.value = NULL;
    coro->wait_node.freed_by_destroy = false;
    enqueue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail, coro, ch);
    pthread_mutex_unlock(&ch->wait_queue_lock);
    coco_preempt_unblock_signal();

    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
    coco_yield();

    /* 减少引用计数 */
    coco_channel_unref(ch);

    /* 检查是否被取消 */
    if (coro->cancelled) {
        return COCO_ERROR_CANCELLED;
    }

    /* 恢复后获取值，检查是否被销毁 */
    if (coro->wait_node.freed_by_destroy) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    if (atomic_load_explicit(&ch->closed, memory_order_acquire) && !coro->wait_node.value) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    *value = coro->wait_node.value;
    return COCO_OK;
}

/**
 * coco_channel_send_batch - 批量发送数据（阻塞）
 *
 * @param ch channel 指针
 * @param vals 数据指针数组
 * @param n 发送数量
 * @return 成功发送的数量，负数错误码
 */
int coco_channel_send_batch(coco_channel_t *ch, void **vals, int n) {
    if (!ch || !vals || n <= 0) {
        return COCO_ERROR;
    }

    if (atomic_load_explicit(&ch->closed, memory_order_acquire)) {
        return COCO_ERROR_CHANNEL_CLOSED;
    }

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    ENSURE_IN_CORO_RET(COCO_ERROR_INVALID);

    int sent = 0;

    while (sent < n) {
        void *value = vals[sent];

        /* 优先唤醒普通接收者（无缓冲场景） */
        coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (recv_waiter) {
            recv_waiter->wait_node.value = value;
            enqueue_ready(sched, recv_waiter);
            sent++;
            continue;
        }

        /* 然后唤醒 select 接收者 */
        coco_select_node_t *recv_node = dequeue_select_node(&ch->recv_select_head,
                                                            &ch->recv_select_tail);
        while (recv_node) {
            coco_coro_t *sel_coro = recv_node->coro;
            if (!sel_coro->select_nodes || sel_coro->select_ready_index != -1) {
                recv_node = dequeue_select_node(&ch->recv_select_head, &ch->recv_select_tail);
                continue;
            }

            sel_coro->wait_node.value = value;
            sel_coro->select_ready_index = recv_node->case_index;
            select_dequeue_all(sel_coro);
            cancel_select_timer(sel_coro);
            enqueue_ready(sched, sel_coro);
            sent++;
            break;
        }
        if (recv_node) {
            continue;
        }

        /* 有缓冲 channel: 尝试批量放入缓冲区 */
        if (ch->capacity > 0) {
            int batch = n - sent;
            size_t space = ch->capacity - ch->count;
            if (batch > (int)space) {
                batch = (int)space;
            }

            for (int i = 0; i < batch; i++) {
                ch->buffer[ch->tail] = vals[sent + i];
                ch->tail = (ch->tail + 1) % ch->capacity;
            }
            ch->count += batch;
            sent += batch;

            if (sent < n) {
                /* 缓冲区满，需要 yield */
                if (coro->wait_node.in_use) {
                    return sent > 0 ? sent : COCO_ERROR;
                }

                if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
                    return sent > 0 ? sent : COCO_ERROR_CHANNEL_CLOSED;
                }

                coco_preempt_block_signal();
                pthread_mutex_lock(&ch->wait_queue_lock);
                if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
                    pthread_mutex_unlock(&ch->wait_queue_lock);
                    coco_preempt_unblock_signal();
                    return sent > 0 ? sent : COCO_ERROR_CHANNEL_CLOSED;
                }
                atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
                coro->wait_node.value = vals[sent];
                coro->wait_node.freed_by_destroy = false;
                enqueue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail, coro, ch);
                pthread_mutex_unlock(&ch->wait_queue_lock);
                coco_preempt_unblock_signal();

                atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
                coco_yield();

                coco_channel_unref(ch);

                if (coro->cancelled) {
                    return sent > 0 ? sent : COCO_ERROR_CANCELLED;
                }

                if (coro->wait_node.freed_by_destroy || atomic_load_explicit(&ch->closed, memory_order_acquire)) {
                    return sent > 0 ? sent : COCO_ERROR_CHANNEL_CLOSED;
                }

                /* 被唤醒后继续循环 */
            }
            continue;
        }

        /* 无缓冲 channel 且无接收者: 阻塞等待 */
        if (coro->wait_node.in_use) {
            return sent > 0 ? sent : COCO_ERROR;
        }

        if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
            return sent > 0 ? sent : COCO_ERROR_CHANNEL_CLOSED;
        }

        coco_preempt_block_signal();
        pthread_mutex_lock(&ch->wait_queue_lock);
        if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
            pthread_mutex_unlock(&ch->wait_queue_lock);
            coco_preempt_unblock_signal();
            return sent > 0 ? sent : COCO_ERROR_CHANNEL_CLOSED;
        }
        atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
        coro->wait_node.value = value;
        coro->wait_node.freed_by_destroy = false;
        enqueue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail, coro, ch);
        pthread_mutex_unlock(&ch->wait_queue_lock);
        coco_preempt_unblock_signal();

        atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
        coco_yield();

        coco_channel_unref(ch);

        if (coro->cancelled) {
            return sent > 0 ? sent : COCO_ERROR_CANCELLED;
        }

        if (coro->wait_node.freed_by_destroy || atomic_load_explicit(&ch->closed, memory_order_acquire)) {
            return sent > 0 ? sent : COCO_ERROR_CHANNEL_CLOSED;
        }

        sent++;
    }

    return sent;
}

/**
 * coco_channel_recv_batch - 批量接收数据（阻塞）
 *
 * @param ch channel 指针
 * @param vals 接收数据指针数组
 * @param n 最大接收数量
 * @param received 输出实际接收数量
 * @return COCO_OK 成功，负数错误码
 */
int coco_channel_recv_batch(coco_channel_t *ch, void **vals, int n, int *received) {
    if (!ch || !vals || n <= 0 || !received) {
        return COCO_ERROR;
    }

    *received = 0;

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    ENSURE_IN_CORO_RET(COCO_ERROR_INVALID);

    while (*received < n) {
        /* 检查 channel 是否已关闭且为空 */
        if (atomic_load_explicit(&ch->closed, memory_order_acquire) && ch->count == 0 && !ch->send_wait_head && !ch->send_select_head) {
            return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
        }

        /* 有缓冲 channel: 尝试批量从缓冲区取 */
        if (ch->capacity > 0 && ch->count > 0) {
            int batch = n - *received;
            if (batch > (int)ch->count) {
                batch = (int)ch->count;
            }

            for (int i = 0; i < batch; i++) {
                vals[*received + i] = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
            }
            ch->count -= batch;
            *received += batch;

            /* 尝试唤醒等待的发送者 */
            coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
            if (send_waiter) {
                ch->buffer[ch->tail] = send_waiter->wait_node.value;
                ch->tail = (ch->tail + 1) % ch->capacity;
                ch->count++;
                enqueue_ready(sched, send_waiter);
            } else {
                coco_select_node_t *send_node = dequeue_select_node(&ch->send_select_head,
                                                                    &ch->send_select_tail);
                while (send_node) {
                    coco_coro_t *sel_coro = send_node->coro;
                    if (!sel_coro->select_nodes || sel_coro->select_ready_index != -1) {
                        send_node = dequeue_select_node(&ch->send_select_head,
                                                        &ch->send_select_tail);
                        continue;
                    }

                    ch->buffer[ch->tail] = send_node->send_val;
                    ch->tail = (ch->tail + 1) % ch->capacity;
                    ch->count++;

                    sel_coro->select_ready_index = send_node->case_index;
                    select_dequeue_all(sel_coro);
                    cancel_select_timer(sel_coro);
                    enqueue_ready(sched, sel_coro);
                    break;
                }
            }

            if (*received < n) {
                /* 需要更多数据，检查是否关闭 */
                if (atomic_load_explicit(&ch->closed, memory_order_acquire) && ch->count == 0 && !ch->send_wait_head && !ch->send_select_head) {
                    return COCO_OK;
                }

                /* 还有空间但缓冲区空，需要 yield */
                if (coro->wait_node.in_use) {
                    return *received > 0 ? COCO_OK : COCO_ERROR;
                }

                if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
                    return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
                }

                coco_preempt_block_signal();
                pthread_mutex_lock(&ch->wait_queue_lock);
                if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
                    pthread_mutex_unlock(&ch->wait_queue_lock);
                    coco_preempt_unblock_signal();
                    return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
                }
                atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
                coro->wait_node.value = NULL;
                coro->wait_node.freed_by_destroy = false;
                enqueue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail, coro, ch);
                pthread_mutex_unlock(&ch->wait_queue_lock);
                coco_preempt_unblock_signal();

                atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
                coco_yield();

                coco_channel_unref(ch);

                if (coro->cancelled) {
                    return *received > 0 ? COCO_OK : COCO_ERROR_CANCELLED;
                }

                if (coro->wait_node.freed_by_destroy) {
                    return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
                }

                /* 被唤醒后检查是否有值 */
                if (coro->wait_node.value) {
                    vals[*received] = coro->wait_node.value;
                    (*received)++;
                }
            }
            continue;
        }

        /* 无缓冲 channel: 优先检查普通发送者 */
        coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (send_waiter) {
            vals[*received] = send_waiter->wait_node.value;
            (*received)++;
            enqueue_ready(sched, send_waiter);
            continue;
        }

        /* 然后检查 select 发送者 */
        coco_select_node_t *send_node = dequeue_select_node(&ch->send_select_head,
                                                            &ch->send_select_tail);
        while (send_node) {
            coco_coro_t *sel_coro = send_node->coro;
            if (!sel_coro->select_nodes || sel_coro->select_ready_index != -1) {
                send_node = dequeue_select_node(&ch->send_select_head,
                                                &ch->send_select_tail);
                continue;
            }

            vals[*received] = send_node->send_val;
            (*received)++;
            sel_coro->select_ready_index = send_node->case_index;
            select_dequeue_all(sel_coro);
            cancel_select_timer(sel_coro);
            enqueue_ready(sched, sel_coro);
            break;
        }
        if (send_node) {
            continue;
        }

        /* 无发送者: 阻塞等待 */
        if (coro->wait_node.in_use) {
            return *received > 0 ? COCO_OK : COCO_ERROR;
        }

        if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
            return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
        }

        coco_preempt_block_signal();
        pthread_mutex_lock(&ch->wait_queue_lock);
        if (atomic_load_explicit(&ch->destroying, memory_order_acquire)) {
            pthread_mutex_unlock(&ch->wait_queue_lock);
            coco_preempt_unblock_signal();
            return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
        }
        atomic_fetch_add_explicit(&ch->refcount, 1, memory_order_relaxed);
        coro->wait_node.value = NULL;
        coro->wait_node.freed_by_destroy = false;
        enqueue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail, coro, ch);
        pthread_mutex_unlock(&ch->wait_queue_lock);
        coco_preempt_unblock_signal();

        atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
        coco_yield();

        coco_channel_unref(ch);

        if (coro->cancelled) {
            return *received > 0 ? COCO_OK : COCO_ERROR_CANCELLED;
        }

        if (coro->wait_node.freed_by_destroy) {
            return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
        }

        if (atomic_load_explicit(&ch->closed, memory_order_acquire) && !coro->wait_node.value) {
            return *received > 0 ? COCO_OK : COCO_ERROR_CHANNEL_CLOSED;
        }

        vals[*received] = coro->wait_node.value;
        (*received)++;
    }

    return COCO_OK;
}

/**
 * coco_channel_close - 关闭 channel
 *
 * @param ch channel 指针
 */
void coco_channel_close(coco_channel_t *ch) {
    if (!ch || atomic_exchange_explicit(&ch->closed, 1, memory_order_acq_rel)) {
        return;
    }
    coco_sched_t *sched = g_current_sched;

    pthread_mutex_lock(&ch->wait_queue_lock);

    /* 唤醒所有普通等待的接收者 */
    while (ch->recv_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (sched && coro) {
            enqueue_ready(sched, coro);
        }
    }

    /* 唤醒所有 select 等待的接收者 */
    while (ch->recv_select_head) {
        coco_select_node_t *node = dequeue_select_node(&ch->recv_select_head,
                                                       &ch->recv_select_tail);
        if (node) {
            coco_coro_t *sel_coro = node->coro;
            if (sel_coro && sel_coro->select_ready_index == -1) {
                sel_coro->select_ready_index = node->case_index;
                select_dequeue_all(sel_coro);
                cancel_select_timer(sel_coro);
                enqueue_ready(sched, sel_coro);
            }
        }
    }

    /* 唤醒所有普通等待的发送者 */
    while (ch->send_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (sched && coro) {
            enqueue_ready(sched, coro);
        }
    }

    /* 唤醒所有 select 等待的发送者 */
    while (ch->send_select_head) {
        coco_select_node_t *node = dequeue_select_node(&ch->send_select_head,
                                                       &ch->send_select_tail);
        if (node) {
            coco_coro_t *sel_coro = node->coro;
            if (sel_coro && sel_coro->select_ready_index == -1) {
                sel_coro->select_ready_index = node->case_index;
                select_dequeue_all(sel_coro);
                cancel_select_timer(sel_coro);
                enqueue_ready(sched, sel_coro);
            }
        }
    }

    pthread_mutex_unlock(&ch->wait_queue_lock);
}

/**
 * coco_channel_select - Go 风格的 channel select
 *
 * @param cases   select case 数组
 * @param ncases  case 数量
 * @param timeout_ms 超时时间（0 = 无超时）
 * @param has_default 是否有 default case
 * @return 就绪 case 的索引，COCO_SELECT_TIMEOUT，或 COCO_SELECT_DEFAULT
 */
int coco_channel_select(coco_select_case_t *cases, int ncases,
                        uint64_t timeout_ms, int has_default) {
    if (!cases || ncases <= 0) {
        return COCO_ERROR_INVALID;
    }

    ENSURE_IN_CORO_RET(COCO_ERROR_INVALID);

    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    /* Phase 1: Non-blocking check — scan for immediately ready operations */
    for (int i = 0; i < ncases; i++) {
        coco_channel_t *ch = cases[i].chan;
        if (!ch) continue;

        if (cases[i].dir == COCO_SELECT_RECV) {
            /* RECV: buffered channel has data */
            if (ch->capacity > 0 && ch->count > 0) {
                void *val = ch->buffer[ch->head];
                ch->head = (ch->head + 1) % ch->capacity;
                ch->count--;
                *(void **)cases[i].val = val;
                cases[i].result = COCO_OK;

                /* Buffer freed a slot — try to wake a waiting sender */
                coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
                if (send_waiter) {
                    ch->buffer[ch->tail] = send_waiter->wait_node.value;
                    ch->tail = (ch->tail + 1) % ch->capacity;
                    ch->count++;
                    enqueue_ready(sched, send_waiter);
                } else {
                    coco_select_node_t *send_node = dequeue_select_node(&ch->send_select_head,
                                                                        &ch->send_select_tail);
                    if (send_node) {
                        coco_coro_t *sel_coro = send_node->coro;
                        if (sel_coro->select_nodes && sel_coro->select_ready_index == -1) {
                            ch->buffer[ch->tail] = send_node->send_val;
                            ch->tail = (ch->tail + 1) % ch->capacity;
                            ch->count++;
                            sel_coro->select_ready_index = send_node->case_index;
                            select_dequeue_all(sel_coro);
                            cancel_select_timer(sel_coro);
                            enqueue_ready(sched, sel_coro);
                        }
                    }
                }

                return i;
            }
            /* RECV: unbuffered channel with waiting sender */
            if (ch->capacity == 0 && ch->send_wait_head) {
                coco_coro_t *send_waiter = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
                if (send_waiter) {
                    *(void **)cases[i].val = send_waiter->wait_node.value;
                    enqueue_ready(sched, send_waiter);
                    cases[i].result = COCO_OK;
                    return i;
                }
            }
        } else {
            /* SEND: channel closed */
            if (atomic_load_explicit(&ch->closed, memory_order_acquire)) {
                cases[i].result = COCO_ERROR_CHANNEL_CLOSED;
                return i;
            }
            /* SEND: buffered channel has space */
            if (ch->capacity > 0 && ch->count < ch->capacity) {
                ch->buffer[ch->tail] = cases[i].val;
                ch->tail = (ch->tail + 1) % ch->capacity;
                ch->count++;
                cases[i].result = COCO_OK;

                /* Buffer has data — try to wake a waiting receiver */
                coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
                if (recv_waiter) {
                    recv_waiter->wait_node.value = ch->buffer[ch->head];
                    ch->head = (ch->head + 1) % ch->capacity;
                    ch->count--;
                    enqueue_ready(sched, recv_waiter);
                } else {
                    coco_select_node_t *recv_node = dequeue_select_node(&ch->recv_select_head,
                                                                        &ch->recv_select_tail);
                    if (recv_node) {
                        coco_coro_t *sel_coro = recv_node->coro;
                        if (sel_coro->select_nodes && sel_coro->select_ready_index == -1) {
                            sel_coro->wait_node.value = ch->buffer[ch->head];
                            ch->head = (ch->head + 1) % ch->capacity;
                            ch->count--;
                            sel_coro->select_ready_index = recv_node->case_index;
                            select_dequeue_all(sel_coro);
                            cancel_select_timer(sel_coro);
                            enqueue_ready(sched, sel_coro);
                        }
                    }
                }

                return i;
            }
            /* SEND: unbuffered channel with waiting receiver */
            if (ch->capacity == 0 && ch->recv_wait_head) {
                coco_coro_t *recv_waiter = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
                if (recv_waiter) {
                    recv_waiter->wait_node.value = cases[i].val;
                    enqueue_ready(sched, recv_waiter);
                    cases[i].result = COCO_OK;
                    return i;
                }
            }
        }
    }

    /* Default case: return immediately if no case is ready */
    if (has_default) {
        return COCO_SELECT_DEFAULT;
    }

    /* Phase 2: Register — no case ready, block */
    coco_select_node_t *nodes = malloc(sizeof(coco_select_node_t) * ncases);
    if (!nodes) {
        return COCO_ERROR_NOMEM;
    }

    /* Mutual exclusion: select_nodes and wait_node.in_use cannot both be active */
    if (coro->wait_node.in_use) {
        free(nodes);
        return COCO_ERROR;
    }

    for (int i = 0; i < ncases; i++) {
        coco_channel_t *ch = cases[i].chan;
        nodes[i].chan = ch;
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

        if (!ch) continue;

        /* Register on channel's select wait queue */
        if (nodes[i].is_send) {
            if (atomic_load_explicit(&ch->closed, memory_order_acquire)) {
                /* Channel closed during registration — cleanup and return */
                for (int j = 0; j < i; j++) {
                    if (nodes[j].registered && nodes[j].chan) {
                        coco_channel_t *jch = nodes[j].chan;
                        if (nodes[j].is_send) {
                            remove_select_node(&jch->send_select_head,
                                               &jch->send_select_tail, &nodes[j]);
                        } else {
                            remove_select_node(&jch->recv_select_head,
                                               &jch->recv_select_tail, &nodes[j]);
                        }
                    }
                }
                cases[i].result = COCO_ERROR_CHANNEL_CLOSED;
                free(nodes);
                return i;
            }
            enqueue_select_node(&ch->send_select_head, &ch->send_select_tail, &nodes[i]);
        } else {
            enqueue_select_node(&ch->recv_select_head, &ch->recv_select_tail, &nodes[i]);
        }
    }

    coro->select_nodes = nodes;
    coro->select_case_count = ncases;
    coro->select_ready_index = -1;
    coro->select_timer = NULL;

    /* Register timeout if requested */
    if (timeout_ms > 0) {
        select_timeout_arg_t *ta = malloc(sizeof(select_timeout_arg_t));
        if (!ta) {
            /* H7: 分配失败时清理已注册的 select 节点 */
            select_dequeue_all(coro);
            free(nodes);
            coro->select_nodes = NULL;
            coro->select_case_count = 0;
            return COCO_ERROR_NOMEM;
        }
        ta->sched = sched;
        ta->coro = coro;
        atomic_init(&ta->callback_executed, false);
        atomic_init(&ta->callback_done, false);
        coro->select_timer = coco_timer_ex(sched, timeout_ms, select_timeout_cb, ta);
        if (!coro->select_timer) {
            /* H7: 定时器创建失败时清理 */
            free(ta);
            select_dequeue_all(coro);
            free(nodes);
            coro->select_nodes = NULL;
            coro->select_case_count = 0;
            return COCO_ERROR_NOMEM;
        }
    }

    atomic_store_explicit(&coro->state, COCO_STATE_WAITING, memory_order_release);
    coco_yield();

    /* Phase 4: Resume — check what woke us */
    int ready_index = coro->select_ready_index;

    /* Cancel timer if still active */
    if (coro->select_timer) {
        /* Free the timeout arg struct (callback would have freed it if fired) */
        cancel_select_timer(coro);
    }

    if (ready_index >= 0) {
        /* A specific case is ready — execute the operation */
        coco_select_node_t *node = &nodes[ready_index];
        coco_channel_t *ch = node->chan;
        if (node->is_send) {
            /* Value was transferred during wakeup or buffered */
            cases[ready_index].result = atomic_load_explicit(&ch->closed, memory_order_acquire) ? COCO_ERROR_CHANNEL_CLOSED : COCO_OK;
        } else {
            *node->recv_ptr = coro->wait_node.value;
            cases[ready_index].result = (coro->wait_node.value == NULL && atomic_load_explicit(&ch->closed, memory_order_acquire))
                                        ? COCO_ERROR_CHANNEL_CLOSED : COCO_OK;
        }
    }
    /* COCO_SELECT_TIMEOUT: already dequeued by timeout callback */

    /* Free select nodes */
    coro->select_nodes = NULL;
    coro->select_case_count = 0;
    coro->select_ready_index = -1;
    free(nodes);

    return ready_index;
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

    /* 设置销毁标志，防止新的等待者入队 */
    atomic_store_explicit(&ch->destroying, true, memory_order_release);

    coco_sched_t *sched = g_current_sched;

    /* 持锁排空等待队列 */
    coco_preempt_block_signal();
    pthread_mutex_lock(&ch->wait_queue_lock);

    /* 清理普通等待队列，设置 freed_by_destroy 标志并唤醒 */
    while (ch->send_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->send_wait_head, &ch->send_wait_tail);
        if (coro) {
            coro->wait_node.freed_by_destroy = true;
            if (sched) {
                enqueue_ready(sched, coro);
            }
        }
    }
    while (ch->recv_wait_head) {
        coco_coro_t *coro = dequeue_wait_coro(&ch->recv_wait_head, &ch->recv_wait_tail);
        if (coro) {
            coro->wait_node.freed_by_destroy = true;
            if (sched) {
                enqueue_ready(sched, coro);
            }
        }
    }

    /* 清理 select 等待队列，清除 chan 指针避免悬空引用 (H5) */
    while (ch->send_select_head) {
        coco_select_node_t *node = dequeue_select_node(&ch->send_select_head,
                                                       &ch->send_select_tail);
        if (node && node->coro) {
            node->chan = NULL;  /* 清除悬空指针 (H5) */
            coco_coro_t *sel_coro = node->coro;
            if (sel_coro->select_ready_index == -1) {
                sel_coro->select_ready_index = node->case_index;
                select_dequeue_all(sel_coro);
                cancel_select_timer(sel_coro);
                if (sched) {
                    enqueue_ready(sched, sel_coro);
                }
            }
        }
    }
    while (ch->recv_select_head) {
        coco_select_node_t *node = dequeue_select_node(&ch->recv_select_head,
                                                       &ch->recv_select_tail);
        if (node && node->coro) {
            node->chan = NULL;  /* 清除悬空指针 (H5) */
            coco_coro_t *sel_coro = node->coro;
            if (sel_coro->select_ready_index == -1) {
                sel_coro->select_ready_index = node->case_index;
                select_dequeue_all(sel_coro);
                cancel_select_timer(sel_coro);
                if (sched) {
                    enqueue_ready(sched, sel_coro);
                }
            }
        }
    }

    pthread_mutex_unlock(&ch->wait_queue_lock);
    coco_preempt_unblock_signal();

    /* 通过 unref 释放，等待者恢复后会调用 unref 减少引用计数 */
    coco_channel_unref(ch);
}
