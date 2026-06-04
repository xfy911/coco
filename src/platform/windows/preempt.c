/**
 * preempt.c - Windows 平台抢占实现
 *
 * 使用 QueueUserAPC + SleepEx 实现异步抢占。
 * Windows 不支持 POSIX 信号，使用 APC 机制。
 */

#include "../../coco_internal.h"
#include <windows.h>
#include <stddef.h>

/* 外部声明 */
void coco_preempt_handler(int sig, siginfo_t *info, void *context);

/* 线程局部定时器状态 */
static _Thread_local struct {
    bool initialized;
    bool armed;
    HANDLE timer_queue;
    HANDLE timer;
    LARGE_INTEGER interval;
} g_windows_preempt;

/* APC 回调函数 */
static VOID CALLBACK preempt_apc_callback(LPVOID lpArgToCompletionRoutine,
                                          DWORD dwTimerLowValue,
                                          DWORD dwTimerHighValue) {
    (void)lpArgToCompletionRoutine;
    (void)dwTimerLowValue;
    (void)dwTimerHighValue;

    /* 模拟信号处理器调用 */
    coco_preempt_handler(0, NULL, NULL);
}

/**
 * coco_preempt_platform_init - Windows 平台初始化
 *
 * 创建定时器队列。
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_platform_init(void) {
    g_windows_preempt.timer_queue = CreateTimerQueue();
    if (!g_windows_preempt.timer_queue) {
        return COCO_ERROR;
    }

    g_windows_preempt.timer = NULL;
    g_windows_preempt.armed = false;
    g_windows_preempt.initialized = true;

    return COCO_OK;
}

/**
 * coco_preempt_platform_cleanup - Windows 平台清理
 */
void coco_preempt_platform_cleanup(void) {
    if (!g_windows_preempt.initialized) {
        return;
    }

    /* 删除定时器 */
    if (g_windows_preempt.timer) {
        DeleteTimerQueueTimer(g_windows_preempt.timer_queue,
                              g_windows_preempt.timer,
                              NULL);
        g_windows_preempt.timer = NULL;
    }

    /* 删除定时器队列 */
    if (g_windows_preempt.timer_queue) {
        DeleteTimerQueue(g_windows_preempt.timer_queue);
        g_windows_preempt.timer_queue = NULL;
    }

    g_windows_preempt.initialized = false;
}

/**
 * coco_preempt_platform_arm - 启用抢占定时器
 *
 * 创建单次定时器，到期后触发 APC。
 *
 * @param interval_ms 定时器间隔（毫秒）
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_platform_arm(uint64_t interval_ms) {
    if (!g_windows_preempt.timer_queue) {
        return COCO_ERROR;
    }

    /* 如果已有定时器，先删除 */
    if (g_windows_preempt.timer) {
        DeleteTimerQueueTimer(g_windows_preempt.timer_queue,
                              g_windows_preempt.timer,
                              NULL);
        g_windows_preempt.timer = NULL;
    }

    /* 创建单次定时器 */
    BOOL result = CreateTimerQueueTimer(&g_windows_preempt.timer,
                                        g_windows_preempt.timer_queue,
                                        preempt_apc_callback,
                                        NULL,
                                        interval_ms,
                                        0,  /* 单次 */
                                        WT_EXECUTEINTIMERTHREAD);

    if (!result) {
        return COCO_ERROR;
    }

    g_windows_preempt.armed = true;

    /* 进入可警报等待状态，允许 APC 执行 */
    /* 这需要在调度循环中调用 SleepEx(0, TRUE) */
    SleepEx(0, TRUE);

    return COCO_OK;
}

/**
 * coco_preempt_platform_disarm - 禁用抢占定时器
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_platform_disarm(void) {
    if (g_windows_preempt.timer) {
        DeleteTimerQueueTimer(g_windows_preempt.timer_queue,
                              g_windows_preempt.timer,
                              NULL);
        g_windows_preempt.timer = NULL;
    }

    g_windows_preempt.armed = false;

    return COCO_OK;
}

/**
 * coco_preempt_sleep_ex - 进入可警报等待状态
 *
 * Windows APC 需要线程处于可警报状态才能执行。
 * 在调度循环中定期调用此函数。
 *
 * @param timeout_ms 等待时间（毫秒）
 */
void coco_preempt_sleep_ex(uint64_t timeout_ms) {
    SleepEx((DWORD)timeout_ms, TRUE);
}

