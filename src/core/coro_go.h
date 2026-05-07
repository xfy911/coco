/**
 * coro_go.h - coco_go API 内部声明 (US-017)
 *
 * 提供简洁的协程启动 API，类似 Go 的 goroutine
 * 公共 API 已在 include/coco.h 中导出
 */

#ifndef COCO_CORO_GO_H
#define COCO_CORO_GO_H

#include "../coco_internal.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* coco_go_opts_t 已在 coco.h 中定义 */

/* 内部函数声明（实现使用） */
coco_coro_t *coco_go_impl(void (*entry)(void*), void *arg,
                           const coco_go_opts_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* COCO_CORO_GO_H */
