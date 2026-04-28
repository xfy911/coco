/**
 * fd_table.c - FD 到协程映射表
 *
 * 动态扩容的 FD 表，初始容量 1024，按需 2x 扩容。
 */

#include "coco_internal.h"
#include <stdlib.h>
#include <string.h>

#define FD_TABLE_INITIAL_CAPACITY 1024

/**
 * fd_table_create - 创建 FD 表
 */
fd_table_t *fd_table_create(uint32_t initial_capacity) {
    fd_table_t *ft = (fd_table_t *)malloc(sizeof(fd_table_t));
    if (!ft) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = FD_TABLE_INITIAL_CAPACITY;
    }

    ft->table = (coco_coro_t **)calloc(initial_capacity, sizeof(coco_coro_t *));
    if (!ft->table) {
        free(ft);
        return NULL;
    }

    ft->capacity = initial_capacity;
    ft->max_fd = 0;

    return ft;
}

/**
 * fd_table_destroy - 销毁 FD 表
 */
void fd_table_destroy(fd_table_t *ft) {
    if (ft) {
        free(ft->table);
        free(ft);
    }
}

/**
 * fd_table_ensure_capacity - 确保容量足够
 *
 * @return 0 成功，负数错误码
 */
static int fd_table_ensure_capacity(fd_table_t *ft, int fd) {
    if ((uint32_t)fd < ft->capacity) {
        return 0;
    }

    /* 计算新容量：2x 扩容直到足够 */
    uint32_t new_capacity = ft->capacity;
    while (new_capacity <= (uint32_t)fd) {
        new_capacity *= 2;
    }

    coco_coro_t **new_table = (coco_coro_t **)realloc(ft->table, new_capacity * sizeof(coco_coro_t *));
    if (!new_table) {
        return COCO_ERROR;
    }

    /* 清零新增部分 */
    memset(new_table + ft->capacity, 0, (new_capacity - ft->capacity) * sizeof(coco_coro_t *));

    ft->table = new_table;
    ft->capacity = new_capacity;

    return COCO_OK;
}

/**
 * fd_table_get - 获取 FD 对应的协程
 */
coco_coro_t *fd_table_get(fd_table_t *ft, int fd) {
    if (!ft || fd < 0 || (uint32_t)fd >= ft->capacity) {
        return NULL;
    }
    return ft->table[fd];
}

/**
 * fd_table_set - 设置 FD 对应的协程
 *
 * @return 0 成功，负数错误码
 */
int fd_table_set(fd_table_t *ft, int fd, coco_coro_t *coro) {
    if (!ft || fd < 0) {
        return COCO_ERROR;
    }

    /* 确保容量足够 */
    if (fd_table_ensure_capacity(ft, fd) < 0) {
        return COCO_ERROR;
    }

    ft->table[fd] = coro;

    /* 更新最大 FD */
    if (coro && (uint32_t)fd > ft->max_fd) {
        ft->max_fd = (uint32_t)fd;
    }

    return COCO_OK;
}

/**
 * fd_table_clear - 清除 FD 对应的协程
 */
void fd_table_clear(fd_table_t *ft, int fd) {
    if (!ft || fd < 0 || (uint32_t)fd >= ft->capacity) {
        return;
    }
    ft->table[fd] = NULL;
}
