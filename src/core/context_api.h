/**
 * context_api.h - Context API (Phase 2, US-009)
 *
 * Go-style context 模式，支持取消传播和超时控制。
 */

#ifndef CONTEXT_API_H
#define CONTEXT_API_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

/* Context 结构 */
typedef struct coco_context {
    /* 父 context */
    struct coco_context *parent;

    /* 取消状态 */
    _Atomic bool cancelled;
    _Atomic bool done;

    /* 取消原因 */
    char *error;

    /* 子 context 列表 */
    struct coco_context **children;
    uint32_t children_count;
    uint32_t children_capacity;
    pthread_mutex_t children_lock;

    /* 超时 */
    int64_t deadline;  /* 纳秒时间戳，0 表示无超时 */

    /* 引用计数 */
    _Atomic uint32_t refcount;

    /* 取消回调 */
    void (*cancel_cb)(struct coco_context *ctx, void *data);
    void *cancel_cb_data;
} coco_context_t;

/* Context 创建选项 */
typedef struct {
    coco_context_t *parent;    /* 父 context */
    int64_t timeout_ms;        /* 超时时间（毫秒），0 表示无超时 */
} coco_context_opts_t;

/* API */

/* 创建 context */
coco_context_t *coco_context_create(coco_context_opts_t *opts);

/* 引用管理 */
coco_context_t *coco_context_ref(coco_context_t *ctx);
void coco_context_unref(coco_context_t *ctx);

/* 取消 */
void coco_context_cancel(coco_context_t *ctx);
bool coco_context_is_cancelled(coco_context_t *ctx);

/* 完成 */
bool coco_context_is_done(coco_context_t *ctx);
void coco_context_wait_done(coco_context_t *ctx);

/* 超时 */
int64_t coco_context_deadline(coco_context_t *ctx);
bool coco_context_has_deadline(coco_context_t *ctx);

/* 子 context */
int coco_context_add_child(coco_context_t *parent, coco_context_t *child);
int coco_context_remove_child(coco_context_t *parent, coco_context_t *child);

/* 回调 */
void coco_context_set_cancel_callback(coco_context_t *ctx,
                                       void (*cb)(coco_context_t *, void *),
                                       void *data);

/* 便捷函数 */
coco_context_t *coco_context_with_timeout(coco_context_t *parent, int64_t timeout_ms);
coco_context_t *coco_context_with_cancel(coco_context_t *parent);
coco_context_t *coco_context_background(void);
coco_context_t *coco_context_todo(void);

#endif /* CONTEXT_API_H */
