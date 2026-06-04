/**
 * preempt.c - 协程异步抢占核心实现
 *
 * 使用信号机制实现异步抢占，确保长时间运行的协程
 * 不会阻塞调度器超过 10ms。
 */

#include "../coco_internal.h"
#include "stack_pool.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <pthread.h>

/* 抢占间隔（毫秒） */
#define COCO_PREEMPT_INTERVAL_MS 10

/* 线程局部抢占状态 */
static _Thread_local struct {
    bool initialized;
    bool enabled;
    volatile sig_atomic_t preempt_pending;
    volatile sig_atomic_t in_handler;
    stack_t preempt_stack;
} g_preempt_state;

/* 前向声明平台特定函数 */
int coco_preempt_platform_init(void);
void coco_preempt_platform_cleanup(void);
int coco_preempt_platform_arm(uint64_t interval_ms);
int coco_preempt_platform_disarm(void);

/* 内部 API 声明 */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/**
 * coco_preempt_handler - 抢占信号处理函数
 *
 * 当定时器到期时被调用，设置抢占标记。
 */
void coco_preempt_handler(int sig, siginfo_t *info, void *context) {
    (void)sig;
    (void)info;
    (void)context;

    /* 防止递归进入 */
    if (g_preempt_state.in_handler) {
        return;
    }
    g_preempt_state.in_handler = 1;

    /* 检查是否启用抢占 */
    if (!g_preempt_state.enabled) {
        g_preempt_state.in_handler = 0;
        return;
    }

    /* 获取当前协程 */
    coco_coro_t *coro = g_current_coro;
    if (!coro || coro->state != COCO_STATE_RUNNING) {
        g_preempt_state.in_handler = 0;
        return;
    }

    /* 标记抢占待处理 */
    g_preempt_state.preempt_pending = 1;

    g_preempt_state.in_handler = 0;
}

/**
 * coco_preempt_init - 初始化抢占子系统
 *
 * 设置信号栈和信号处理器。
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_init(void) {
    if (g_preempt_state.initialized) {
        return COCO_OK;
    }

    /* 分配信号栈 */
    size_t stack_size = SIGSTKSZ * 2;
    g_preempt_state.preempt_stack.ss_sp = malloc(stack_size);
    if (!g_preempt_state.preempt_stack.ss_sp) {
        return COCO_ERROR_NOMEM;
    }
    g_preempt_state.preempt_stack.ss_size = stack_size;
    g_preempt_state.preempt_stack.ss_flags = 0;

    /* 安装信号栈 */
    if (sigaltstack(&g_preempt_state.preempt_stack, NULL) != 0) {
        free(g_preempt_state.preempt_stack.ss_sp);
        return COCO_ERROR;
    }

    /* 平台特定初始化 */
    int ret = coco_preempt_platform_init();
    if (ret != COCO_OK) {
        stack_t disable = {0};
        disable.ss_flags = SS_DISABLE;
        sigaltstack(&disable, NULL);
        free(g_preempt_state.preempt_stack.ss_sp);
        return ret;
    }

    g_preempt_state.initialized = true;
    g_preempt_state.enabled = false;
    g_preempt_state.preempt_pending = 0;
    g_preempt_state.in_handler = 0;

    return COCO_OK;
}

/**
 * coco_preempt_cleanup - 清理抢占子系统
 */
void coco_preempt_cleanup(void) {
    if (!g_preempt_state.initialized) {
        return;
    }

    coco_preempt_platform_cleanup();

    /* 禁用信号栈 */
    stack_t disable = {0};
    disable.ss_flags = SS_DISABLE;
    sigaltstack(&disable, NULL);

    /* 释放信号栈 */
    if (g_preempt_state.preempt_stack.ss_sp) {
        free(g_preempt_state.preempt_stack.ss_sp);
        g_preempt_state.preempt_stack.ss_sp = NULL;
    }

    g_preempt_state.initialized = false;
}

/**
 * coco_preempt_arm - 启用抢占定时器
 *
 * 在切换到协程前调用，设置抢占定时器。
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_arm(void) {
    if (!g_preempt_state.initialized) {
        return COCO_ERROR;
    }

    g_preempt_state.enabled = true;
    g_preempt_state.preempt_pending = 0;

    return coco_preempt_platform_arm(COCO_PREEMPT_INTERVAL_MS);
}

/**
 * coco_preempt_disarm - 禁用抢占定时器
 *
 * 在切换回调度器后调用，取消抢占定时器。
 *
 * @return COCO_OK 成功，负数错误码失败
 */
int coco_preempt_disarm(void) {
    if (!g_preempt_state.initialized) {
        return COCO_ERROR;
    }

    g_preempt_state.enabled = false;
    g_preempt_state.preempt_pending = 0;

    return coco_preempt_platform_disarm();
}

/**
 * coco_preempt_enable - 启用当前协程的抢占
 */
void coco_preempt_enable(void) {
    g_preempt_state.enabled = true;
}

/**
 * coco_preempt_disable - 禁用当前协程的抢占
 *
 * 用于栈增长等关键区域。
 */
void coco_preempt_disable(void) {
    g_preempt_state.enabled = false;
}

/**
 * coco_preempt_is_pending - 检查是否有待处理的抢占
 *
 * @return 1 如果有待处理的抢占，0 否则
 */
int coco_preempt_is_pending(void) {
    return g_preempt_state.preempt_pending;
}

/**
 * coco_preempt_checkpoint - 抢占检查点
 *
 * 在协程中显式调用，允许检查抢占。
 * 协作式抢占点。
 */
void coco_preempt_checkpoint(void) {
    if (g_preempt_state.preempt_pending && g_preempt_state.enabled) {
        g_preempt_state.preempt_pending = 0;
        coco_yield();
    }
}

#ifndef _WIN32
static sigset_t preempt_sigset(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
#ifdef SIGURG
    sigaddset(&set, SIGURG);
#endif
    return set;
}

/**
 * coco_preempt_block_signal - 阻塞抢占信号
 *
 * 使用 pthread_sigmask 阻塞 SIGALRM（和 SIGURG），
 * 保护关键区域免受抢占信号中断。
 *
 * @return COCO_OK 成功，COCO_ERROR 失败
 */
int coco_preempt_block_signal(void) {
    sigset_t set = preempt_sigset();
    return pthread_sigmask(SIG_BLOCK, &set, NULL) == 0 ? COCO_OK : COCO_ERROR;
}

/**
 * coco_preempt_unblock_signal - 解除阻塞抢占信号
 *
 * 使用 pthread_sigmask 解除阻塞 SIGALRM（和 SIGURG），
 * 恢复抢占信号的正常投递。
 *
 * @return COCO_OK 成功，COCO_ERROR 失败
 */
int coco_preempt_unblock_signal(void) {
    sigset_t set = preempt_sigset();
    return pthread_sigmask(SIG_UNBLOCK, &set, NULL) == 0 ? COCO_OK : COCO_ERROR;
}
#else
int coco_preempt_block_signal(void) { return COCO_OK; }
int coco_preempt_unblock_signal(void) { return COCO_OK; }
#endif
