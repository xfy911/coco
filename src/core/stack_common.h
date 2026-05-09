/**
 * stack_common.h - 共享栈基础设施
 *
 * 提供栈分配、释放、清零和使用率检测的公共函数，
 * 供 stack.c / stack_pool.c / stack_pool_multi.c / stack_pool_mt.c 共用。
 */

#ifndef STACK_COMMON_H
#define STACK_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Size classes: 2KB, 4KB, 8KB, 16KB, 32KB, 64KB, 128KB, 256KB, 512KB, 1MB */
#define STACK_SIZE_2K    (2 * 1024)
#define STACK_SIZE_4K    (4 * 1024)
#define STACK_SIZE_8K    (8 * 1024)
#define STACK_SIZE_16K   (16 * 1024)
#define STACK_SIZE_32K   (32 * 1024)
#define STACK_SIZE_64K   (64 * 1024)
#define STACK_SIZE_128K  (128 * 1024)
#define STACK_SIZE_256K  (256 * 1024)
#define STACK_SIZE_512K  (512 * 1024)
#define STACK_SIZE_1M    (1024 * 1024)

/* 最小、默认和最大尺寸 */
#define STACK_SIZE_MIN     STACK_SIZE_2K
#define STACK_SIZE_DEFAULT STACK_SIZE_2K
#define STACK_SIZE_MAX     STACK_SIZE_1M

/* 栈池选择性清零模式 */
typedef enum {
    STACK_ZERO_NONE = 0,      /* 不清零（最快） */
    STACK_ZERO_TOP_1K = 1,    /* 仅清零栈顶 1KB（推荐） */
    STACK_ZERO_FULL = 2       /* 清零全部（最安全） */
} stack_zero_mode_t;

/* 公共栈操作函数 */
size_t get_page_size(void);
void *alloc_stack_mmap(size_t size);
void free_stack_mmap(void *stack_top, size_t size);
void zero_stack(void *stack_top, size_t size, stack_zero_mode_t mode);
size_t get_usage(void *stack_top, size_t size, void *current_sp);

#endif /* STACK_COMMON_H */
