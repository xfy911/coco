/**
 * context_coro.h - Context 与协程集成 (Phase 2, US-010)
 *
 * 将 context 与协程关联，支持 context 取消时中断协程操作。
 */

#ifndef CONTEXT_CORO_H
#define CONTEXT_CORO_H

#include "context_api.h"
#include "../coco_internal.h"

/* 协程 Context API */

/* 设置协程的 context */
int coco_coro_set_context(coco_coro_t *coro, coco_context_t *ctx);

/* 获取协程的 context */
coco_context_t *coco_coro_get_context(coco_coro_t *coro);

/* 检查协程是否应该取消 */
bool coco_coro_should_cancel(coco_coro_t *coro);

/* Context 感知的 I/O 操作 */

/* 带 context 的读取 */
ssize_t coco_read_with_context(coco_context_t *ctx, int fd, void *buf, size_t count);

/* 带 context 的写入 */
ssize_t coco_write_with_context(coco_context_t *ctx, int fd, const void *buf, size_t count);

/* 带 context 的 channel 发送 */
int coco_channel_send_with_context(coco_context_t *ctx, void *ch, void *value);

/* 带 context 的 channel 接收 */
int coco_channel_recv_with_context(coco_context_t *ctx, void *ch, void **value);

/* 带 context 的等待 */
int coco_wait_with_context(coco_context_t *ctx, int fd, uint32_t events, int timeout_ms);

#endif /* CONTEXT_CORO_H */
