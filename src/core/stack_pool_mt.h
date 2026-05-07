/**
 * stack_pool_mt.h - 多尺寸栈池 (多线程模式, Phase 4)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * Per-P 设计，每个处理器有独立的栈池，避免竞争
 *
 * 栈所有权规则:
 * - 栈由 P 的栈池分配，归还到同一个 P 的栈池
 * - 协程迁移时，栈所有权随协程迁移
 * - 跨 P 释放时，通过全局缓存延迟归还
 */

#ifndef STACK_POOL_MT_H
#define STACK_POOL_MT_H

#include "stack_common.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

/* 栈池大小类 - 8 种尺寸 */
#define STACK_POOL_MT_NUM_CLASSES  8
#define STACK_POOL_MT_CLASS_LIMIT  256  /* 每个 size class 上限 */

/* 空闲栈节点 - 嵌入栈空间 */
typedef struct stack_node_mt {
    void *stack_top;              /* 栈顶地址 */
    size_t size;                  /* 栈大小 */
    uint32_t owner_p_id;          /* 所属 P 的 ID */
    struct stack_node_mt *next;   /* 链表下一个 */
} stack_node_mt_t;

/* Per-P 栈池结构 (无锁，单线程访问) */
typedef struct stack_pool_per_p {
    stack_node_mt_t *freelists[STACK_POOL_MT_NUM_CLASSES];  /* 空闲链表 */
    size_t sizes[STACK_POOL_MT_NUM_CLASSES];                /* Size class 大小 */
    uint32_t counts[STACK_POOL_MT_NUM_CLASSES];             /* 当前数量 */
    uint32_t limits[STACK_POOL_MT_NUM_CLASSES];             /* 上限 */
    stack_zero_mode_t zero_mode;                         /* 清零模式 */
    uint32_t p_id;                                          /* 所属 P 的 ID */

    /* 统计信息 */
    uint64_t total_allocs;      /* 总分配次数 */
    uint64_t total_frees;       /* 总释放次数 */
    uint64_t pool_hits;         /* 池命中次数 */
    uint64_t pool_misses;       /* 池未命中次数 */
} stack_pool_per_p_t;

/* 全局栈缓存 (跨 P 释放时使用) */
typedef struct stack_pool_global_cache {
    stack_node_mt_t *freelists[STACK_POOL_MT_NUM_CLASSES];
    pthread_mutex_t locks[STACK_POOL_MT_NUM_CLASSES];
    uint32_t counts[STACK_POOL_MT_NUM_CLASSES];
    uint32_t limits[STACK_POOL_MT_NUM_CLASSES];  /* 全局缓存上限 */
} stack_pool_global_cache_t;

/* API */

/* Per-P 栈池 */
stack_pool_per_p_t *stack_pool_per_p_create(uint32_t p_id);
void stack_pool_per_p_destroy(stack_pool_per_p_t *pool);
void *stack_pool_per_p_alloc(stack_pool_per_p_t *pool, size_t size);
void stack_pool_per_p_free(stack_pool_per_p_t *pool, void *stack_top, size_t size);

/* 全局缓存 */
int stack_pool_global_cache_init(void);
void stack_pool_global_cache_destroy(void);
void stack_pool_global_cache_push(void *stack_top, size_t size, uint32_t p_id);
stack_node_mt_t *stack_pool_global_cache_pop(int class_idx);

/* 栈使用率检测 */
size_t stack_pool_mt_get_usage(void *stack_top, size_t size, void *current_sp);

/* 统计 API */
void stack_pool_per_p_get_stats(stack_pool_per_p_t *pool,
                                uint64_t *total_allocs,
                                uint64_t *total_frees,
                                uint64_t *pool_hits,
                                uint64_t *pool_misses);

/* Size class 辅助函数 */
int stack_pool_mt_get_class_index(size_t size);
size_t stack_pool_mt_get_class_size(int class_idx);

#endif /* STACK_POOL_MT_H */