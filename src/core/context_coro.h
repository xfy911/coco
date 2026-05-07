/**
 * context_coro.h - Context 与协程集成 (Phase 2, US-010)
 *
 * 将 context 与协程关联，支持 context 取消时中断协程操作。
 */

#ifndef CONTEXT_CORO_H
#define CONTEXT_CORO_H

#include "context_api.h"
#include "../coco_internal.h"

/* 设置协程的 context */
int coco_coro_set_context(coco_coro_t *coro, coco_context_t *ctx);

/* 获取协程的 context */
coco_context_t *coco_coro_get_context(coco_coro_t *coro);

/* 检查协程是否应该取消 */
bool coco_coro_should_cancel(coco_coro_t *coro);

#endif /* CONTEXT_CORO_H */
