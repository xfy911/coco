/**
 * stack_pool_multi.c - 多尺寸栈池原型实现 (Phase 0 验证)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * 单线程设计，用于验证栈池扩展可行性
 */

#include "stack_common.h"
#include "stack_pool_multi.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Size class 大小表 */
static const size_t size_class_table[STACK_POOL_MULTI_NUM_CLASSES] = {
    STACK_SIZE_8K,
    STACK_SIZE_16K,
    STACK_SIZE_32K,
    STACK_SIZE_64K,
    STACK_SIZE_128K,
    STACK_SIZE_256K,
    STACK_SIZE_512K,
    STACK_SIZE_1M
};

/* 获取 size class 索引 */
int stack_pool_multi_get_class_index(size_t size) {
    for (int i = 0; i < STACK_POOL_MULTI_NUM_CLASSES; i++) {
        if (size <= size_class_table[i]) {
            return i;
        }
    }
    return -1;  /* 超出范围 */
}

/* 获取 size class 大小 */
size_t stack_pool_multi_get_class_size(int class_idx) {
    if (class_idx < 0 || class_idx >= STACK_POOL_MULTI_NUM_CLASSES) {
        return 0;
    }
    return size_class_table[class_idx];
}

/* 创建栈池 */
stack_pool_multi_t *stack_pool_multi_create(void) {
    stack_pool_multi_t *pool = calloc(1, sizeof(stack_pool_multi_t));
    if (!pool) {
        return NULL;
    }

    /* 初始化 size classes */
    for (int i = 0; i < STACK_POOL_MULTI_NUM_CLASSES; i++) {
        pool->sizes[i] = size_class_table[i];
        pool->limits[i] = STACK_POOL_MULTI_CLASS_LIMIT;
        pool->freelists[i] = NULL;
        pool->counts[i] = 0;
    }

    /* 默认使用栈顶 1KB 清零模式 */
    pool->zero_mode = STACK_ZERO_TOP_1K;

    /* 初始化统计 */
    pool->total_allocs = 0;
    pool->total_frees = 0;
    pool->pool_hits = 0;
    pool->pool_misses = 0;

    return pool;
}

/* 销毁栈池 */
void stack_pool_multi_destroy(stack_pool_multi_t *pool) {
    if (!pool) {
        return;
    }

    /* 释放所有空闲栈 */
    for (int i = 0; i < STACK_POOL_MULTI_NUM_CLASSES; i++) {
        stack_node_multi_t *node = pool->freelists[i];
        while (node) {
            stack_node_multi_t *next = node->next;
            free_stack_mmap(node->stack_top, node->size);
            node = next;
        }
    }

    free(pool);
}

/* 从栈池分配 */
void *stack_pool_multi_alloc(stack_pool_multi_t *pool, size_t size) {
    if (!pool) {
        return NULL;
    }

    pool->total_allocs++;

    int class_idx = stack_pool_multi_get_class_index(size);

    /* 超出池范围，直接 mmap */
    if (class_idx < 0) {
        pool->pool_misses++;
        return alloc_stack_mmap(size);
    }

    /* 尝试从空闲链表获取 */
    stack_node_multi_t *node = pool->freelists[class_idx];
    if (node) {
        pool->freelists[class_idx] = node->next;
        pool->counts[class_idx]--;
        pool->pool_hits++;

        void *stack_top = node->stack_top;
        size_t actual_size = node->size;

        /* 选择性清零栈 */
        zero_stack(stack_top, actual_size, pool->zero_mode);

        return stack_top;
    }

    /* 空闲链表为空，分配新栈 */
    pool->pool_misses++;
    return alloc_stack_mmap(pool->sizes[class_idx]);
}

/* 释放栈到池 */
void stack_pool_multi_free(stack_pool_multi_t *pool, void *stack_top, size_t size) {
    if (!pool || !stack_top) {
        return;
    }

    pool->total_frees++;

    int class_idx = stack_pool_multi_get_class_index(size);

    /* 超出池范围，直接 munmap */
    if (class_idx < 0) {
        free_stack_mmap(stack_top, size);
        return;
    }

    /* 池已满，直接 munmap */
    if (pool->counts[class_idx] >= pool->limits[class_idx]) {
        free_stack_mmap(stack_top, size);
        return;
    }

    /* 将节点嵌入栈空间底部（guard page 之后的位置） */
    size_t page_size = get_page_size();
    size_t actual_size = pool->sizes[class_idx];
    void *stack_base = (void*)((uintptr_t)stack_top - actual_size - page_size);
    stack_node_multi_t *node = (stack_node_multi_t*)((uintptr_t)stack_base + page_size);

    node->stack_top = stack_top;
    node->size = actual_size;
    node->next = pool->freelists[class_idx];
    pool->freelists[class_idx] = node;
    pool->counts[class_idx]++;
}

/* 栈使用率检测 API - 委托给公共函数 */
size_t stack_pool_multi_get_usage(void *stack_top, size_t size, void *current_sp) {
    return get_usage(stack_top, size, current_sp);
}

/* 获取统计信息 */
void stack_pool_multi_get_stats(stack_pool_multi_t *pool,
                                uint64_t *total_allocs,
                                uint64_t *total_frees,
                                uint64_t *pool_hits,
                                uint64_t *pool_misses) {
    if (!pool) {
        return;
    }

    if (total_allocs) *total_allocs = pool->total_allocs;
    if (total_frees) *total_frees = pool->total_frees;
    if (pool_hits) *pool_hits = pool->pool_hits;
    if (pool_misses) *pool_misses = pool->pool_misses;
}