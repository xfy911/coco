/**
 * stack_pool_multi.h - 多尺寸栈池原型 (Phase 0 验证)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * 单线程设计，用于验证栈池扩展可行性
 */

#ifndef STACK_POOL_MULTI_H
#define STACK_POOL_MULTI_H

#include "stack_common.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* 栈池大小类 - 8 种尺寸 */
#define STACK_POOL_MULTI_NUM_CLASSES  8
#define STACK_POOL_MULTI_CLASS_LIMIT  256  /* 每个 size class 上限 */

/* 空闲栈节点 - 嵌入栈空间 */
typedef struct stack_node_multi {
    void *stack_top;              /* 栈顶地址 */
    size_t size;                  /* 栈大小 */
    struct stack_node_multi *next; /* 链表下一个 */
} stack_node_multi_t;

/* 栈池结构 */
typedef struct stack_pool_multi {
    stack_node_multi_t *freelists[STACK_POOL_MULTI_NUM_CLASSES];  /* 空闲链表 */
    size_t sizes[STACK_POOL_MULTI_NUM_CLASSES];                    /* Size class 大小 */
    uint32_t counts[STACK_POOL_MULTI_NUM_CLASSES];                 /* 当前数量 */
    uint32_t limits[STACK_POOL_MULTI_NUM_CLASSES];                 /* 上限 */
    stack_zero_mode_t zero_mode;                                   /* 清零模式 */

    /* 统计信息 — 原子变量，无需锁 */
    _Atomic uint64_t total_allocs;      /* 总分配次数 */
    _Atomic uint64_t total_frees;       /* 总释放次数 */
    _Atomic uint64_t pool_hits;         /* 池命中次数 */
    _Atomic uint64_t pool_misses;       /* 池未命中次数 */

    pthread_mutex_t locks[STACK_POOL_MULTI_NUM_CLASSES];  /* 每类一把锁 */
} stack_pool_multi_t;

/* API */
stack_pool_multi_t *stack_pool_multi_create(void);
void stack_pool_multi_destroy(stack_pool_multi_t *pool);
void *stack_pool_multi_alloc(stack_pool_multi_t *pool, size_t size);
void stack_pool_multi_free(stack_pool_multi_t *pool, void *stack_top, size_t size);

/* 栈使用率检测 */
size_t stack_pool_multi_get_usage(void *stack_top, size_t size, void *current_sp);

/* 统计 API */
void stack_pool_multi_get_stats(stack_pool_multi_t *pool,
                                uint64_t *total_allocs,
                                uint64_t *total_frees,
                                uint64_t *pool_hits,
                                uint64_t *pool_misses);

/* Size class 辅助函数 */
int stack_pool_multi_get_class_index(size_t size);
size_t stack_pool_multi_get_class_size(int class_idx);

#endif /* STACK_POOL_MULTI_H */