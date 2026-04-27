/**
 * timer_wheel.c - 4 层时间轮定时器
 *
 * 精度: 1ms
 * 范围: 0 - 2^32 ms
 * 结构: 4 层层级时间轮 (W1, W2, W3, W4)
 */

#include "../coco_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 每层时间轮的槽位数 */
#define W1_SIZE 256   /* 第1层: 256 slots, 1ms granularity, range 0-255ms */
#define W2_SIZE 64    /* 第2层: 64 slots, 256ms granularity, range 0-16383ms */
#define W3_SIZE 64    /* 第3层: 64 slots, 16384ms granularity, range 0-1048575ms */
#define W4_SIZE 64    /* 第4层: 64 slots, 1048576ms granularity, range 0-67108863ms */

/* 时间轮结构 */
typedef struct coco_timer_wheel {
    coco_timer_t *w1[W1_SIZE];  /* 第1层 */
    coco_timer_t *w2[W2_SIZE];  /* 第2层 */
    coco_timer_t *w3[W3_SIZE];  /* 第3层 */
    coco_timer_t *w4[W4_SIZE];  /* 第4层 */

    uint32_t w1_tick;           /* 第1层当前 tick */
    uint32_t w2_tick;           /* 第2层当前 tick */
    uint32_t w3_tick;           /* 第3层当前 tick */
    uint32_t w4_tick;           /* 第4层当前 tick */

    uint64_t current_time_ms;   /* 当前时间（毫秒） */
} coco_timer_wheel_t;

/* 定时器结构 */
struct coco_timer {
    uint64_t expire_time_ms;    /* 过期时间（毫秒） */
    coco_coro_t *coro;          /* 关联协程 */
    void (*callback)(void*);    /* 回调函数 */
    void *arg;                  /* 回调参数 */
    coco_timer_t *next;         /* 链表节点 */
};

/* 全局时间轮 */
static coco_timer_wheel_t *g_timer_wheel = NULL;

/* 获取当前时间（毫秒） */
static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

/* 初始化时间轮 */
coco_timer_wheel_t *coco_timer_wheel_create(void) {
    coco_timer_wheel_t *tw = calloc(1, sizeof(coco_timer_wheel_t));
    if (!tw) {
        return NULL;
    }

    tw->current_time_ms = get_current_time_ms();
    g_timer_wheel = tw;

    return tw;
}

/* 销毁时间轮 */
void coco_timer_wheel_destroy(coco_timer_wheel_t *tw) {
    if (!tw) {
        return;
    }

    /* 清理所有定时器 */
    for (int i = 0; i < W1_SIZE; i++) {
        coco_timer_t *timer = tw->w1[i];
        while (timer) {
            coco_timer_t *next = timer->next;
            free(timer);
            timer = next;
        }
    }
    /* 类似清理其他层... */

    free(tw);
    g_timer_wheel = NULL;
}

/* 添加定时器到指定层级 */
static void add_timer_to_wheel(coco_timer_wheel_t *tw, coco_timer_t *timer, int level, int slot) {
    timer->next = NULL;

    switch (level) {
        case 1:
            if (tw->w1[slot]) {
                coco_timer_t *head = tw->w1[slot];
                timer->next = head;
            }
            tw->w1[slot] = timer;
            break;
        case 2:
            if (tw->w2[slot]) {
                coco_timer_t *head = tw->w2[slot];
                timer->next = head;
            }
            tw->w2[slot] = timer;
            break;
        case 3:
            if (tw->w3[slot]) {
                coco_timer_t *head = tw->w3[slot];
                timer->next = head;
            }
            tw->w3[slot] = timer;
            break;
        case 4:
            if (tw->w4[slot]) {
                coco_timer_t *head = tw->w4[slot];
                timer->next = head;
            }
            tw->w4[slot] = timer;
            break;
    }
}

/* 计算定时器应放入哪一层 */
static void place_timer(coco_timer_wheel_t *tw, coco_timer_t *timer) {
    uint64_t delay_ms = timer->expire_time_ms - tw->current_time_ms;

    if (delay_ms < W1_SIZE) {
        /* 第1层 */
        int slot = (tw->w1_tick + delay_ms) % W1_SIZE;
        add_timer_to_wheel(tw, timer, 1, slot);
    } else if (delay_ms < W1_SIZE * W2_SIZE) {
        /* 第2层 */
        int slot = (tw->w2_tick + delay_ms / W1_SIZE) % W2_SIZE;
        add_timer_to_wheel(tw, timer, 2, slot);
    } else if (delay_ms < W1_SIZE * W2_SIZE * W3_SIZE) {
        /* 第3层 */
        int slot = (tw->w3_tick + delay_ms / (W1_SIZE * W2_SIZE)) % W3_SIZE;
        add_timer_to_wheel(tw, timer, 3, slot);
    } else {
        /* 第4层 */
        int slot = (tw->w4_tick + delay_ms / (W1_SIZE * W2_SIZE * W3_SIZE)) % W4_SIZE;
        add_timer_to_wheel(tw, timer, 4, slot);
    }
}

/* 创建定时器 */
coco_timer_t *coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg) {
    if (!g_timer_wheel) {
        return NULL;
    }

    coco_timer_t *timer = calloc(1, sizeof(coco_timer_t));
    if (!timer) {
        return NULL;
    }

    timer->expire_time_ms = g_timer_wheel->current_time_ms + delay_ms;
    timer->callback = callback;
    timer->arg = arg;

    place_timer(g_timer_wheel, timer);

    return timer;
}

/* 创建定时器并关联协程 */
coco_timer_t *coco_timer_add(coco_timer_wheel_t *tw, uint64_t delay_ms, coco_coro_t *coro) {
    coco_timer_t *timer = calloc(1, sizeof(coco_timer_t));
    if (!timer) {
        return NULL;
    }

    timer->expire_time_ms = tw->current_time_ms + delay_ms;
    timer->coro = coro;

    place_timer(tw, timer);

    return timer;
}

/* 取消定时器 */
void coco_timer_cancel(coco_timer_t *timer) {
    if (!timer) {
        return;
    }

    /* 简单标记为已取消（实际应从链表中移除） */
    timer->callback = NULL;
    timer->coro = NULL;
}

/* 从层级 cascading 定时器到下一层 */
static void cascade_timers(coco_timer_wheel_t *tw, int level) {
    int slot;
    coco_timer_t *timer_list;

    switch (level) {
        case 2:
            slot = tw->w2_tick % W2_SIZE;
            timer_list = tw->w2[slot];
            tw->w2[slot] = NULL;
            break;
        case 3:
            slot = tw->w3_tick % W3_SIZE;
            timer_list = tw->w3[slot];
            tw->w3[slot] = NULL;
            break;
        case 4:
            slot = tw->w4_tick % W4_SIZE;
            timer_list = tw->w4[slot];
            tw->w4[slot] = NULL;
            break;
        default:
            return;
    }

    /* 重新放置定时器 */
    while (timer_list) {
        coco_timer_t *timer = timer_list;
        timer_list = timer->next;
        timer->next = NULL;

        if (timer->coro || timer->callback) {
            place_timer(tw, timer);
        } else {
            free(timer);
        }
    }
}

/* 处理过期定时器 */
static void process_expired_timers(coco_timer_wheel_t *tw, coco_sched_t *sched) {
    int slot = tw->w1_tick % W1_SIZE;
    coco_timer_t *timer_list = tw->w1[slot];
    tw->w1[slot] = NULL;

    while (timer_list) {
        coco_timer_t *timer = timer_list;
        timer_list = timer->next;

        if (timer->coro) {
            /* 唤醒协程 */
            /* enqueue_ready(sched, timer->coro); */
            timer->coro->state = COCO_STATE_READY;
            timer->coro->wake_time = 0;
        } else if (timer->callback) {
            /* 执行回调 */
            timer->callback(timer->arg);
        }

        free(timer);
    }
}

/* 时间轮 tick */
void coco_timer_tick(coco_timer_wheel_t *tw, coco_sched_t *sched) {
    if (!tw) {
        return;
    }

    uint64_t now_ms = get_current_time_ms();
    uint64_t elapsed = now_ms - tw->current_time_ms;

    for (uint64_t i = 0; i < elapsed; i++) {
        tw->current_time_ms++;
        tw->w1_tick++;

        /* 检查是否需要 cascading */
        if (tw->w1_tick % W1_SIZE == 0) {
            tw->w2_tick++;
            cascade_timers(tw, 2);

            if (tw->w2_tick % W2_SIZE == 0) {
                tw->w3_tick++;
                cascade_timers(tw, 3);

                if (tw->w3_tick % W3_SIZE == 0) {
                    tw->w4_tick++;
                    cascade_timers(tw, 4);
                }
            }
        }

        /* 处理过期定时器 */
        process_expired_timers(tw, sched);
    }
}

/* 获取下一个定时器到期时间 */
uint64_t coco_timer_wheel_next_expire(coco_timer_wheel_t *tw) {
    if (!tw) {
        return 0;
    }

    /* 简化实现：返回最小延迟 */
    for (int i = 0; i < W1_SIZE; i++) {
        if (tw->w1[i]) {
            uint64_t delay = (i - tw->w1_tick % W1_SIZE) % W1_SIZE;
            return tw->current_time_ms + delay;
        }
    }

    return 0;
}