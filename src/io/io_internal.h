/**
 * io_internal.h - I/O 内部共享代码
 *
 * 提供跨平台的 I/O 辅助函数和宏：
 * - kernel_version_at_least: Linux 内核版本检测
 * - 批量 I/O stub: 非 io_uring 后端的空实现
 * - I/O 配置 API: coco_sched_set_io_options / coco_sched_get_io_options
 * - coco_iouring_get_stats 默认实现
 */

#ifndef IO_INTERNAL_H
#define IO_INTERNAL_H

#include "../coco_internal.h"
#include <stdbool.h>

/* === Linux 内核版本检测 === */

#ifdef __linux__
#include <sys/utsname.h>
#include <stdio.h>

/**
 * kernel_version_at_least - 检测内核版本是否 >= 指定版本
 *
 * @param major 主版本号
 * @param minor 次版本号
 * @return true 如果内核版本 >= 指定版本
 */
static inline bool kernel_version_at_least(int major, int minor) {
    struct utsname uts;
    if (uname(&uts) != 0) return false;

    int kmajor = 0, kminor = 0, kpatch = 0;
    sscanf(uts.release, "%d.%d.%d", &kmajor, &kminor, &kpatch);

    if (kmajor > major) return true;
    if (kmajor == major && kminor > minor) return true;
    if (kmajor == major && kminor == minor) return true;
    return false;
}
#endif /* __linux__ */

/* === 批量 I/O stub (非 io_uring 后端) === */

/**
 * COCO_BATCH_IO_STUBS - 批量 I/O API 空实现宏
 *
 * 用于 kqueue (macOS) 和 WSAPoll (Windows) 后端，
 * 这些后端不支持批量 I/O。
 */
#define COCO_BATCH_IO_STUBS \
coco_batch_io_t *coco_batch_begin(coco_sched_t *sched) { \
    (void)sched; \
    return NULL; \
} \
\
int coco_batch_add_read(coco_batch_io_t *batch, int fd, void *buf, size_t count) { \
    (void)batch; (void)fd; (void)buf; (void)count; \
    return COCO_ERROR; \
} \
\
int coco_batch_add_write(coco_batch_io_t *batch, int fd, const void *buf, size_t count) { \
    (void)batch; (void)fd; (void)buf; (void)count; \
    return COCO_ERROR; \
} \
\
int coco_batch_submit(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results) { \
    (void)batch; (void)results; (void)max_results; \
    return COCO_ERROR; \
} \
\
int coco_batch_cancel(coco_batch_io_t *batch) { \
    (void)batch; \
    return COCO_ERROR; \
} \
\
void coco_batch_end(coco_batch_io_t *batch) { \
    (void)batch; \
}

/* === I/O 配置 API 默认实现 === */

/**
 * coco_sched_set_io_options_impl - 设置 I/O 配置选项 (默认实现)
 *
 * @param sched 调度器
 * @param options 配置选项
 * @return COCO_OK 成功，COCO_ERROR 失败
 *
 * 必须在调度器初始化之前调用。
 */
static inline int coco_sched_set_io_options_impl(coco_sched_t *sched, const coco_io_options_t *options) {
    if (!sched || !options) {
        return COCO_ERROR;
    }

    /* 必须在调度器初始化之前调用 */
    if (sched->poll_fd >= 0) {
        return COCO_ERROR;
    }

    sched->io_options = *options;
    sched->io_options_set = true;

    return COCO_OK;
}

/**
 * coco_sched_get_io_options_impl - 获取 I/O 配置选项 (默认实现)
 *
 * @param sched 调度器
 * @param options 输出配置
 * @return COCO_OK 成功，COCO_ERROR 失败
 */
static inline int coco_sched_get_io_options_impl(coco_sched_t *sched, coco_io_options_t *options) {
    if (!sched || !options) {
        return COCO_ERROR;
    }

    /* 非 io_uring 后端返回默认配置 */
    options->queue_depth = 256;
    options->sqpoll_enabled = false;
    options->sqpoll_cpu = -1;
    options->sqpoll_idle_ms = 0;

    if (sched->io_options_set) {
        *options = sched->io_options;
    }

    return COCO_OK;
}

/**
 * coco_iouring_get_stats_default - io_uring 统计信息默认实现 (清零)
 *
 * 用于非 Linux 平台或非 io_uring 后端。
 */
static inline void coco_iouring_get_stats_default(uint64_t *submit_count, uint64_t *syscall_count) {
    if (submit_count) *submit_count = 0;
    if (syscall_count) *syscall_count = 0;
}

#endif /* IO_INTERNAL_H */