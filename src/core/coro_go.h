/**
 * coro_go.h - coco_go API 声明 (US-017)
 *
 * 提供简洁的协程启动 API，类似 Go 的 goroutine
 */

#ifndef COCO_CORO_GO_H
#define COCO_CORO_GO_H

#include "../coco_internal.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 协程启动选项
 */
typedef struct coco_go_opts {
    size_t stack_size;          /* 栈大小（0 = 默认） */
    struct coco_context *context; /* 关联的 context（可选） */
    int priority;               /* 优先级（-1 = 默认） */
    int p_id;                   /* 指定 P（-1 = 自动选择） */
} coco_go_opts_t;

/**
 * @brief 启动协程（自动选择最佳 P）
 * @param entry 入口函数
 * @param arg 入口参数
 * @return 协程句柄，或 NULL 失败
 *
 * 在多线程调度器中，自动选择负载最低的 P。
 * 在单线程调度器中，使用当前调度器。
 */
coco_coro_t *coco_go(void (*entry)(void*), void *arg);

/**
 * @brief 在指定 P 上启动协程
 * @param p_id P 的 ID
 * @param entry 入口函数
 * @param arg 入口参数
 * @return 协程句柄，或 NULL 失败
 *
 * 显式指定 P，用于需要数据局部性的场景。
 */
coco_coro_t *coco_go_on(int p_id, void (*entry)(void*), void *arg);

/**
 * @brief 带选项启动协程
 * @param entry 入口函数
 * @param arg 入口参数
 * @param opts 选项（可为 NULL）
 * @return 协程句柄，或 NULL 失败
 */
coco_coro_t *coco_go_with_opts(void (*entry)(void*), void *arg,
                                const coco_go_opts_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* COCO_CORO_GO_H */
