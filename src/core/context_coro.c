/**
 * context_coro.c - Context 与协程集成实现 (Phase 2, US-010)
 *
 * 仅保留协程 context 关联的核心功能。
 * I/O 和 channel 的 context 感知包装已移除（无外部调用者）。
 */

#include "context_coro.h"
#include <unistd.h>
#include <errno.h>

/* 外部全局变量 */
extern _Thread_local coco_sched_t *g_current_sched;
extern _Thread_local coco_coro_t *g_current_coro;

/**
 * coco_coro_set_context - 设置协程的 context
 */
int coco_coro_set_context(coco_coro_t *coro, coco_context_t *ctx) {
    if (!coro) {
        return COCO_ERROR;
    }

    /* 如果已有 context，减少引用 */
    if (coro->context) {
        coco_context_unref(coro->context);
    }

    /* 设置新 context */
    coro->context = ctx ? coco_context_ref(ctx) : NULL;

    return COCO_OK;
}

/**
 * coco_coro_get_context - 获取协程的 context
 */
coco_context_t *coco_coro_get_context(coco_coro_t *coro) {
    if (!coro) {
        return NULL;
    }
    return coro->context;
}

/**
 * coco_coro_should_cancel - 检查协程是否应该取消
 */
bool coco_coro_should_cancel(coco_coro_t *coro) {
    if (!coro) {
        return true;
    }

    /* 检查协程自身的取消标志 */
    if (coro->cancelled) {
        return true;
    }

    /* 检查关联的 context */
    if (coro->context && coco_context_is_cancelled(coro->context)) {
        return true;
    }

    return false;
}
