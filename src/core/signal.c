/**
 * signal.c - 栈溢出信号处理
 *
 * 使用 SIGSEGV handler + sigaltstack 检测栈溢出。
 * 当协程栈溢出时，从 fault address 反查协程并标记为 OVERFLOW 状态。
 */

#include "../coco_internal.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>

/* 线程局部跳转缓冲区（用于恢复到调度器） */
static _Thread_local sigjmp_buf g_overflow_jmp;

/* 线程局部调度器指针（用于恢复） */
static _Thread_local coco_sched_t *g_overflow_sched = NULL;

/* 信号栈大小（需要足够大以运行 handler） */
#define SIGNAL_STACK_SIZE (64 * 1024)

/* 替代信号栈 */
static stack_t g_sigaltstack;

/**
 * find_coro_by_stack - 从 fault address 反查协程
 *
 * @param sched 调度器
 * @param fault_addr 触发 SIGSEGV 的地址
 * @return 协程指针，未找到返回 NULL
 */
static coco_coro_t *find_coro_by_stack(coco_sched_t *sched, void *fault_addr) {
    if (!sched || !fault_addr) {
        return NULL;
    }

    uintptr_t addr = (uintptr_t)fault_addr;

    /* 遍历协程池 */
    for (uint32_t i = 0; i < sched->coro_capacity; i++) {
        coco_coro_t *coro = sched->coro_table[i];
        if (!coro || !coro->stack_base) {
            continue;
        }

        uintptr_t base = (uintptr_t)coro->stack_base;
        uintptr_t top = (uintptr_t)coro->stack_top;
        size_t page_size = sysconf(_SC_PAGESIZE);

        /* 检查 fault address 是否在 guard page 范围内 */
        /* guard page 位于 stack_base 到 stack_base + page_size */
        if (addr >= base && addr < base + page_size) {
            return coro;
        }

        /* 也检查是否在栈范围内（其他栈错误） */
        if (addr >= base && addr < top) {
            return coro;
        }
    }

    return NULL;
}

/**
 * segv_handler - SIGSEGV 信号处理函数
 *
 * 检测栈溢出，标记协程状态，恢复到调度器。
 */
static void segv_handler(int sig, siginfo_t *info, void *context) {
    (void)sig;
    (void)context;

    void *fault_addr = info->si_addr;
    coco_sched_t *sched = g_overflow_sched;

    if (!sched) {
        /* 无调度器，无法恢复，终止进程 */
        _exit(139);  /* 128 + 11 (SIGSEGV) */
    }

    /* 反查溢出的协程 */
    coco_coro_t *coro = find_coro_by_stack(sched, fault_addr);

    if (coro) {
        /* 标记为栈溢出 */
        coro->state = COCO_STATE_OVERFLOW;

        /* 调用错误回调（如果有） */
        if (coro->error_cb) {
            coro->error_cb(coro, COCO_ERROR_STACK_OVERFLOW, "Stack overflow detected");
        }

        /* 恢复到调度器 */
        siglongjmp(g_overflow_jmp, 1);
    } else {
        /* 不是栈溢出，是其他段错误，终止进程 */
        _exit(139);
    }
}

/**
 * coco_signal_init - 初始化信号处理
 *
 * @param sched 调度器指针（用于恢复）
 * @return 0 成功，负数错误码
 */
int coco_signal_init(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    g_overflow_sched = sched;

    /* 设置替代信号栈（避免在溢出的栈上运行 handler） */
    g_sigaltstack.ss_sp = malloc(SIGNAL_STACK_SIZE);
    if (!g_sigaltstack.ss_sp) {
        return COCO_ERROR_NOMEM;
    }
    g_sigaltstack.ss_size = SIGNAL_STACK_SIZE;
    g_sigaltstack.ss_flags = 0;

    if (sigaltstack(&g_sigaltstack, NULL) != 0) {
        free(g_sigaltstack.ss_sp);
        return COCO_ERROR;
    }

    /* 设置 SIGSEGV handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;  /* 使用替代栈 */
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        free(g_sigaltstack.ss_sp);
        return COCO_ERROR;
    }

    return COCO_OK;
}

/**
 * coco_signal_cleanup - 清理信号处理
 */
void coco_signal_cleanup(void) {
    /* 恢复默认 SIGSEGV handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);

    /* 禁用替代信号栈 */
    stack_t disable;
    disable.ss_sp = NULL;
    disable.ss_size = 0;
    disable.ss_flags = SS_DISABLE;
    sigaltstack(&disable, NULL);

    /* 释放信号栈 */
    if (g_sigaltstack.ss_sp) {
        free(g_sigaltstack.ss_sp);
        g_sigaltstack.ss_sp = NULL;
    }

    g_overflow_sched = NULL;
}

/**
 * coco_set_overflow_checkpoint - 设置溢出恢复点
 *
 * 在调度器切换到协程前调用，保存跳转点。
 * 如果协程溢出，handler 会恢复到此点。
 *
 * @return 0 正常返回，1 从溢出恢复返回
 */
int coco_set_overflow_checkpoint(void) {
    return sigsetjmp(g_overflow_jmp, 0);
}