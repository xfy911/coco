/**
 * preempt.c - macOS 平台抢占实现
 *
 * 使用 SIGURG 信号和 setitimer 实现异步抢占。
 * macOS 与 Linux 基本一致。
 */

#include "../../coco_internal.h"
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>

/* 抢占信号 */
#define COCO_PREEMPT_SIGNAL SIGURG

/* 外部声明 */
void coco_preempt_handler(int sig, siginfo_t *info, void *context);

/* 线程局部定时器状态 */
static _Thread_local struct {
    bool armed;
    struct sigaction old_sa;
} g_macos_preempt;

/**
 * sigurg_handler - SIGURG 信号处理器包装
 */
static void sigurg_handler(int sig, siginfo_t *info, void *context) {
    coco_preempt_handler(sig, info, context);
}

/**
 * coco_preempt_platform_init - macOS 平台初始化
 *
 * 设置 SIGURG 信号处理器。
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_platform_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_sigaction = sigurg_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    /* 阻塞其他信号以防止递归 */
    sigaddset(&sa.sa_mask, COCO_PREEMPT_SIGNAL);

    if (sigaction(COCO_PREEMPT_SIGNAL, &sa, &g_macos_preempt.old_sa) != 0) {
        return COCO_ERROR;
    }

    g_macos_preempt.armed = false;

    return COCO_OK;
}

/**
 * coco_preempt_platform_cleanup - macOS 平台清理
 */
void coco_preempt_platform_cleanup(void) {
    /* 恢复原始信号处理器 */
    sigaction(COCO_PREEMPT_SIGNAL, &g_macos_preempt.old_sa, NULL);

    /* 如果定时器还在运行，禁用它 */
    if (g_macos_preempt.armed) {
        struct itimerval it = {0};
        setitimer(ITIMER_REAL, &it, NULL);
        g_macos_preempt.armed = false;
    }
}

/**
 * coco_preempt_platform_arm - 启用抢占定时器
 *
 * 使用 ITIMER_REAL 设置单次定时器。
 *
 * @param interval_ms 定时器间隔（毫秒）
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_platform_arm(uint64_t interval_ms) {
    struct itimerval it;

    /* 设置首次触发时间和间隔 */
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;  /* 单次定时器 */

    it.it_value.tv_sec = interval_ms / 1000;
    it.it_value.tv_usec = (interval_ms % 1000) * 1000;

    if (setitimer(ITIMER_REAL, &it, NULL) != 0) {
        return COCO_ERROR;
    }

    g_macos_preempt.armed = true;

    return COCO_OK;
}

/**
 * coco_preempt_platform_disarm - 禁用抢占定时器
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_platform_disarm(void) {
    struct itimerval it = {0};

    if (setitimer(ITIMER_REAL, &it, NULL) != 0) {
        return COCO_ERROR;
    }

    g_macos_preempt.armed = false;

    return COCO_OK;
}

/**
 * coco_preempt_block_signal - 阻塞抢占信号
 *
 * 用于栈增长等关键区域。
 *
 * @return COCO_OK 成功
 */
int coco_preempt_block_signal(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, COCO_PREEMPT_SIGNAL);
    return pthread_sigmask(SIG_BLOCK, &set, NULL);
}

/**
 * coco_preempt_unblock_signal - 解除阻塞抢占信号
 *
 * @return COCO_OK 成功
 */
int coco_preempt_unblock_signal(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, COCO_PREEMPT_SIGNAL);
    return pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}