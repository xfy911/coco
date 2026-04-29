/**
 * context_coro.c - Context 与协程集成实现 (Phase 2, US-010)
 */

#include "context_coro.h"
#include "../channel/channel_mt.h"
#include <unistd.h>
#include <errno.h>
#include <poll.h>

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

/**
 * coco_read_with_context - 带 context 的读取
 */
ssize_t coco_read_with_context(coco_context_t *ctx, int fd, void *buf, size_t count) {
    if (!buf || count == 0) {
        return COCO_ERROR;
    }

    /* 检查 context 是否已取消 */
    if (ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    /* 尝试读取 */
    ssize_t n = read(fd, buf, count);

    if (n < 0 && errno == EAGAIN) {
        /* 需要等待，检查 context */
        if (ctx && coco_context_is_cancelled(ctx)) {
            return COCO_ERROR_CANCELLED;
        }
    }

    return n;
}

/**
 * coco_write_with_context - 带 context 的写入
 */
ssize_t coco_write_with_context(coco_context_t *ctx, int fd, const void *buf, size_t count) {
    if (!buf || count == 0) {
        return COCO_ERROR;
    }

    /* 检查 context 是否已取消 */
    if (ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    /* 尝试写入 */
    ssize_t n = write(fd, buf, count);

    if (n < 0 && errno == EAGAIN) {
        /* 需要等待，检查 context */
        if (ctx && coco_context_is_cancelled(ctx)) {
            return COCO_ERROR_CANCELLED;
        }
    }

    return n;
}

/**
 * coco_channel_send_with_context - 带 context 的 channel 发送
 */
int coco_channel_send_with_context(coco_context_t *ctx, void *ch, void *value) {
    if (!ch) {
        return COCO_ERROR;
    }

    /* 检查 context 是否已取消 */
    if (ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    /* 使用多线程 channel API */
    coco_channel_mt_t *mt_ch = (coco_channel_mt_t *)ch;
    int ret = coco_channel_mt_send_thread(mt_ch, value);

    /* 再次检查 context */
    if (ret == COCO_OK && ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    return ret;
}

/**
 * coco_channel_recv_with_context - 带 context 的 channel 接收
 */
int coco_channel_recv_with_context(coco_context_t *ctx, void *ch, void **value) {
    if (!ch || !value) {
        return COCO_ERROR;
    }

    /* 检查 context 是否已取消 */
    if (ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    /* 使用多线程 channel API */
    coco_channel_mt_t *mt_ch = (coco_channel_mt_t *)ch;
    int ret = coco_channel_mt_recv_thread(mt_ch, value);

    /* 再次检查 context */
    if (ret == COCO_OK && ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    return ret;
}

/**
 * coco_wait_with_context - 带 context 的等待
 */
int coco_wait_with_context(coco_context_t *ctx, int fd, uint32_t events, int timeout_ms) {
    if (fd < 0) {
        return COCO_ERROR;
    }

    /* 检查 context 是否已取消 */
    if (ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    /* 简单实现：使用 poll 等待 */
    struct pollfd pfd = {
        .fd = fd,
        .events = (short)(events == 1 ? POLLIN : POLLOUT),
        .revents = 0
    };

    int ret = poll(&pfd, 1, timeout_ms);

    if (ret < 0) {
        return COCO_ERROR;
    }

    if (ret == 0) {
        return COCO_ERROR_WOULD_BLOCK;  /* 超时 */
    }

    /* 再次检查 context */
    if (ctx && coco_context_is_cancelled(ctx)) {
        return COCO_ERROR_CANCELLED;
    }

    return COCO_OK;
}
