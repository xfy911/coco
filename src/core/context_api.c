/**
 * context_api.c - Context API 实现 (Phase 2, US-009)
 *
 * Go-style context 模式实现。
 */

#include "context_api.h"
#include "../coco_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 获取当前时间（纳秒） */
static int64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* 全局 background context */
static coco_context_t *global_background = NULL;
static pthread_once_t background_once = PTHREAD_ONCE_INIT;

static void init_background(void) {
    global_background = calloc(1, sizeof(coco_context_t));
    if (global_background) {
        atomic_store(&global_background->refcount, 1);
        atomic_store(&global_background->cancelled, false);
        atomic_store(&global_background->done, false);
        pthread_mutex_init(&global_background->children_lock, NULL);
    }
}

/**
 * coco_context_create - 创建 context
 */
coco_context_t *coco_context_create(coco_context_opts_t *opts) {
    coco_context_t *ctx = calloc(1, sizeof(coco_context_t));
    if (!ctx) {
        return NULL;
    }

    atomic_store(&ctx->refcount, 1);
    atomic_store(&ctx->cancelled, false);
    atomic_store(&ctx->done, false);

    if (pthread_mutex_init(&ctx->children_lock, NULL) != 0) {
        free(ctx);
        return NULL;
    }

    ctx->children_capacity = 4;
    ctx->children = malloc(ctx->children_capacity * sizeof(coco_context_t*));
    if (!ctx->children) {
        pthread_mutex_destroy(&ctx->children_lock);
        free(ctx);
        return NULL;
    }

    if (opts) {
        ctx->parent = opts->parent ? coco_context_ref(opts->parent) : NULL;

        if (opts->timeout_ms > 0) {
            ctx->deadline = get_time_ns() + opts->timeout_ms * 1000000LL;
        } else {
            ctx->deadline = 0;
        }
    }

    /* 如果有父 context，添加为子 context */
    if (ctx->parent) {
        coco_context_add_child(ctx->parent, ctx);

        /* 如果父 context 已取消，子 context 也取消 */
        if (coco_context_is_cancelled(ctx->parent)) {
            coco_context_cancel(ctx);
        }
    }

    return ctx;
}

/**
 * coco_context_ref - 增加引用计数
 */
coco_context_t *coco_context_ref(coco_context_t *ctx) {
    if (!ctx) {
        return NULL;
    }
    atomic_fetch_add(&ctx->refcount, 1);
    return ctx;
}

/**
 * coco_context_unref - 减少引用计数
 */
void coco_context_unref(coco_context_t *ctx) {
    if (!ctx) {
        return;
    }

    if (atomic_fetch_sub(&ctx->refcount, 1) == 1) {
        /* 引用计数为 0，释放资源 */

        /* 从父 context 移除 */
        if (ctx->parent) {
            coco_context_remove_child(ctx->parent, ctx);
            coco_context_unref(ctx->parent);
        }

        /* 取消所有子 context */
        pthread_mutex_lock(&ctx->children_lock);
        for (uint32_t i = 0; i < ctx->children_count; i++) {
            coco_context_cancel(ctx->children[i]);
            coco_context_unref(ctx->children[i]);
        }
        free(ctx->children);
        pthread_mutex_unlock(&ctx->children_lock);

        pthread_mutex_destroy(&ctx->children_lock);

        if (ctx->error) {
            free(ctx->error);
        }

        free(ctx);
    }
}

/**
 * coco_context_cancel - 取消 context
 */
void coco_context_cancel(coco_context_t *ctx) {
    if (!ctx) {
        return;
    }

    bool expected = false;
    if (!atomic_compare_exchange_strong(&ctx->cancelled, &expected, true)) {
        return;  /* 已经取消 */
    }

    /* 标记完成 */
    atomic_store(&ctx->done, true);

    /* 调用取消回调 */
    if (ctx->cancel_cb) {
        ctx->cancel_cb(ctx, ctx->cancel_cb_data);
    }

    /* 取消所有子 context */
    pthread_mutex_lock(&ctx->children_lock);
    for (uint32_t i = 0; i < ctx->children_count; i++) {
        coco_context_cancel(ctx->children[i]);
    }
    pthread_mutex_unlock(&ctx->children_lock);
}

/**
 * coco_context_is_cancelled - 检查是否已取消
 */
bool coco_context_is_cancelled(coco_context_t *ctx) {
    if (!ctx) {
        return true;
    }

    /* 检查超时 */
    if (ctx->deadline > 0 && get_time_ns() >= ctx->deadline) {
        coco_context_cancel(ctx);
    }

    /* 检查父 context */
    if (ctx->parent && coco_context_is_cancelled(ctx->parent)) {
        coco_context_cancel(ctx);
    }

    return atomic_load(&ctx->cancelled);
}

/**
 * coco_context_is_done - 检查是否已完成
 */
bool coco_context_is_done(coco_context_t *ctx) {
    if (!ctx) {
        return true;
    }

    /* 检查取消状态（会触发超时检查） */
    coco_context_is_cancelled(ctx);

    return atomic_load(&ctx->done);
}

/**
 * coco_context_wait_done - 等待完成
 */
void coco_context_wait_done(coco_context_t *ctx) {
    if (!ctx) {
        return;
    }

    while (!coco_context_is_done(ctx)) {
        struct timespec ts = {0, 1000000};  /* 1ms */
        nanosleep(&ts, NULL);
    }
}

/**
 * coco_context_deadline - 获取截止时间
 */
int64_t coco_context_deadline(coco_context_t *ctx) {
    if (!ctx) {
        return 0;
    }

    if (ctx->deadline > 0) {
        return ctx->deadline;
    }

    if (ctx->parent) {
        return coco_context_deadline(ctx->parent);
    }

    return 0;
}

/**
 * coco_context_has_deadline - 检查是否有截止时间
 */
bool coco_context_has_deadline(coco_context_t *ctx) {
    if (!ctx) {
        return false;
    }

    if (ctx->deadline > 0) {
        return true;
    }

    if (ctx->parent) {
        return coco_context_has_deadline(ctx->parent);
    }

    return false;
}

/**
 * coco_context_add_child - 添加子 context
 */
int coco_context_add_child(coco_context_t *parent, coco_context_t *child) {
    if (!parent || !child) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&parent->children_lock);

    /* 扩容 */
    if (parent->children_count >= parent->children_capacity) {
        uint32_t new_cap = parent->children_capacity * 2;
        coco_context_t **new_children = realloc(parent->children,
                                                 new_cap * sizeof(coco_context_t*));
        if (!new_children) {
            pthread_mutex_unlock(&parent->children_lock);
            return COCO_ERROR_NOMEM;
        }
        parent->children = new_children;
        parent->children_capacity = new_cap;
    }

    parent->children[parent->children_count++] = child;
    pthread_mutex_unlock(&parent->children_lock);

    return COCO_OK;
}

/**
 * coco_context_remove_child - 移除子 context
 */
int coco_context_remove_child(coco_context_t *parent, coco_context_t *child) {
    if (!parent || !child) {
        return COCO_ERROR;
    }

    pthread_mutex_lock(&parent->children_lock);

    for (uint32_t i = 0; i < parent->children_count; i++) {
        if (parent->children[i] == child) {
            /* 移动后面的元素 */
            for (uint32_t j = i; j < parent->children_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children_count--;
            pthread_mutex_unlock(&parent->children_lock);
            return COCO_OK;
        }
    }

    pthread_mutex_unlock(&parent->children_lock);
    return COCO_ERROR;
}

/**
 * coco_context_set_cancel_callback - 设置取消回调
 */
void coco_context_set_cancel_callback(coco_context_t *ctx,
                                       void (*cb)(coco_context_t *, void *),
                                       void *data) {
    if (!ctx) {
        return;
    }

    ctx->cancel_cb = cb;
    ctx->cancel_cb_data = data;
}

/**
 * coco_context_with_timeout - 创建带超时的 context
 */
coco_context_t *coco_context_with_timeout(coco_context_t *parent, int64_t timeout_ms) {
    coco_context_opts_t opts = {
        .parent = parent,
        .timeout_ms = timeout_ms
    };
    return coco_context_create(&opts);
}

/**
 * coco_context_with_cancel - 创建可取消的 context
 */
coco_context_t *coco_context_with_cancel(coco_context_t *parent) {
    coco_context_opts_t opts = {
        .parent = parent,
        .timeout_ms = 0
    };
    return coco_context_create(&opts);
}

/**
 * coco_context_background - 获取 background context
 */
coco_context_t *coco_context_background(void) {
    pthread_once(&background_once, init_background);
    return coco_context_ref(global_background);
}

/* TODO context */
static coco_context_t *todo_ctx = NULL;
static pthread_once_t todo_once = PTHREAD_ONCE_INIT;

static void init_todo(void) {
    todo_ctx = calloc(1, sizeof(coco_context_t));
    if (todo_ctx) {
        atomic_store(&todo_ctx->refcount, 1);
        atomic_store(&todo_ctx->cancelled, false);
        atomic_store(&todo_ctx->done, false);
        pthread_mutex_init(&todo_ctx->children_lock, NULL);
    }
}

/**
 * coco_context_todo - 获取 TODO context
 */
coco_context_t *coco_context_todo(void) {
    /* TODO context 是一个永远不会完成的 context */
    pthread_once(&todo_once, init_todo);
    return coco_context_ref(todo_ctx);
}
