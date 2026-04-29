/**
 * stack_pool_legacy.h - 多尺寸栈池 (单线程模式, Phase 4)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * 单线程设计，保留用于向后兼容和单线程场景
 */

#ifndef STACK_POOL_LEGACY_H
#define STACK_POOL_LEGACY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* 栈池大小类 - 8 种尺寸 */
#define STACK_POOL_LEGACY_NUM_CLASSES  8
#define STACK_POOL_LEGACY_CLASS_LIMIT  256  /* 每个 size class 上限 */

/* Size classes: 8KB, 16KB, 32KB, 64KB, 128KB, 256KB, 512KB, 1MB */
#define STACK_SIZE_8K    (8 * 1024)
#define STACK_SIZE_16K   (16 * 1024)
#define STACK_SIZE_32K   (32 * 1024)
#define STACK_SIZE_64K   (64 * 1024)
#define STACK_SIZE_128K  (128 * 1024)
#define STACK_SIZE_256K  (256 * 1024)
#define STACK_SIZE_512K  (512 * 1024)
#define STACK_SIZE_1M    (1024 * 1024)

/* 最小和默认尺寸 */
#define STACK_SIZE_MIN     STACK_SIZE_8K
#define STACK_SIZE_DEFAULT STACK_SIZE_32K

/* 栈池选择性清零模式 (与 stack_pool_mt.h 共享定义) */
#ifndef STACK_ZERO_MODE_DEFINED
#define STACK_ZERO_MODE_DEFINED
typedef enum stack_zero_mode {
    STACK_ZERO_NONE = 0,      /* 不清零（最快） */
    STACK_ZERO_TOP_1K = 1,    /* 仅清零栈顶 1KB（推荐） */
    STACK_ZERO_FULL = 2       /* 清零全部（最安全） */
} stack_zero_mode_t;
#endif

/* 空闲栈节点 - 嵌入栈空间 */
typedef struct stack_node_legacy {
    void *stack_top;              /* 栈顶地址 */
    size_t size;                  /* 栈大小 */
    struct stack_node_legacy *next; /* 链表下一个 */
} stack_node_legacy_t;

/* 栈池结构 (单线程) */
typedef struct stack_pool_legacy {
    stack_node_legacy_t *freelists[STACK_POOL_LEGACY_NUM_CLASSES];  /* 空闲链表 */
    size_t sizes[STACK_POOL_LEGACY_NUM_CLASSES];                    /* Size class 大小 */
    uint32_t counts[STACK_POOL_LEGACY_NUM_CLASSES];                 /* 当前数量 */
    uint32_t limits[STACK_POOL_LEGACY_NUM_CLASSES];                 /* 上限 */
    stack_zero_mode_t zero_mode;                                   /* 清零模式 */

    /* 统计信息 */
    uint64_t total_allocs;      /* 总分配次数 */
    uint64_t total_frees;       /* 总释放次数 */
    uint64_t pool_hits;         /* 池命中次数 */
    uint64_t pool_misses;       /* 池未命中次数 */
} stack_pool_legacy_t;

/* API (单线程版本) */
stack_pool_legacy_t *stack_pool_legacy_create(void);
void stack_pool_legacy_destroy(stack_pool_legacy_t *pool);
void *stack_pool_legacy_alloc(stack_pool_legacy_t *pool, size_t size);
void stack_pool_legacy_free(stack_pool_legacy_t *pool, void *stack_top, size_t size);

/* 栈使用率检测 */
size_t stack_pool_legacy_get_usage(void *stack_top, size_t size, void *current_sp);

/* 统计 API */
void stack_pool_legacy_get_stats(stack_pool_legacy_t *pool,
                                 uint64_t *total_allocs,
                                 uint64_t *total_frees,
                                 uint64_t *pool_hits,
                                 uint64_t *pool_misses);

/* Size class 辅助函数 */
int stack_pool_legacy_get_class_index(size_t size);
size_t stack_pool_legacy_get_class_size(int class_idx);

#endif /* STACK_POOL_LEGACY_H */