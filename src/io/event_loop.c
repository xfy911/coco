/**
 * event_loop.c - 事件循环核心
 */

#include "../coco_internal.h"

/* 外部全局变量 */
extern coco_sched_t *g_current_sched;
extern coco_coro_t *g_current_coro;

int coco_sleep(uint64_t ms) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return COCO_ERROR;
    }

    /* 注册定时器 */
    coco_timer_add(sched->timer_wheel, ms, coro);

    /* 设置为等待状态并 yield */
    coro->state = COCO_STATE_WAITING;
    coco_yield();

    return COCO_OK;
}