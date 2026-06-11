/**
 * cancel.c - 协程取消机制
 */

#include "../coco_internal.h"
#include "../channel/channel_common.h"
#include <stdlib.h>

/* 外部全局变量（在 coro.c 中定义，使用 TLS） */

/**
 * coco_cancel - 取消协程
 *
 * @param coro 要取消的协程
 * @return COCO_OK 成功，COCO_ERROR 失败
 *
 * 设置取消标志并唤醒协程。协程恢复后应检查 coco_cancelled()。
 */
int coco_cancel(coco_coro_t *coro) {
    if (!coro) {
        return COCO_ERROR;
    }

    /* 死协程或已溢出协程无法取消 */
    coco_state_t st = atomic_load_explicit(&coro->state, memory_order_acquire);
    if (st == COCO_STATE_DEAD || st == COCO_STATE_OVERFLOW) {
        return COCO_ERROR;
    }

    coro->cancelled = 1;

    /* 如果协程在等待状态，唤醒它 */
    coco_sched_t *sched = g_current_sched;
    st = atomic_load_explicit(&coro->state, memory_order_acquire);
    if (st == COCO_STATE_WAITING && sched) {
        /* 清除 wait_fd */
        if (coro->wait_fd >= 0) {
            coco_poll_unregister(sched, coro->wait_fd);
            coro->wait_fd = -1;
        }

        /* 清除 channel 等待队列 */
        if (coro->wait_node.in_use && coro->wait_node.channel) {
            /* channel 字段是 void*，需要使用 coco_channel_cancel_cleanup */
            /* 该函数内部会检查是否是有效的 coco_channel_t */
            coco_channel_cancel_cleanup((coco_channel_t *)coro->wait_node.channel, coro);
        }

        enqueue_ready(sched, coro);
    }

    return COCO_OK;
}

/**
 * coco_cancelled - 检查协程是否被取消
 *
 * @return 1 已取消，0 未取消
 *
 * 协程应在阻塞操作后调用此函数检查取消状态。
 */
int coco_cancelled(void) {
    coco_coro_t *coro = g_current_coro;
    if (!coro) {
        return 0;
    }
    return coro->cancelled;
}