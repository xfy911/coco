/**
 * context_api.h - Context API (Phase 2, US-009, US-014)
 *
 * Go-style context 模式，支持取消传播、超时控制和 value 传播。
 */

#ifndef CONTEXT_API_H
#define CONTEXT_API_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

/* Value 析构函数类型 */
typedef void (*coco_value_destructor_t)(void *value);

/* Context value 节点 */
typedef struct coco_context_value {
    const char *key;
    void *value;
    coco_value_destructor_t destructor;
    struct coco_context_value *next;
} coco_context_value_t;

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

    /* Value 存储 (US-014) */
    coco_context_value_t *values;
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

/* Value API (US-014) */

/**
 * @brief 创建带 value 的 context
 * @param parent 父 context
 * @param key 键名
 * @param value 值
 * @param destructor 析构函数（可为 NULL）
 * @return 新 context，或 NULL 失败
 */
coco_context_t *coco_context_with_value(coco_context_t *parent,
                                         const char *key,
                                         void *value,
                                         coco_value_destructor_t destructor);

/**
 * @brief 获取 context 中的 value
 * @param ctx context 指针
 * @param key 键名
 * @return 值，或 NULL 未找到
 *
 * 沿 context 树向上查找，直到找到匹配的 key。
 */
void *coco_context_value(coco_context_t *ctx, const char *key);

#endif /* CONTEXT_API_H */
