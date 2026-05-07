/**
 * stack_pool_mt.c - 多尺寸栈池实现 (多线程模式, Phase 4)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * Per-P 设计，每个处理器有独立的栈池，避免竞争
 *
 * 栈所有权规则:
 * - 栈由 P 的栈池分配，归还到同一个 P 的栈池
 * - 协程迁移时，栈所有权随协程迁移
 * - 跨 P 释放时，通过全局缓存延迟归还
 */

#include "stack_common.h"
#include "stack_pool_mt.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* 全局缓存实例 */
static stack_pool_global_cache_t g_global_cache;
static bool g_global_cache_initialized = false;

/* Size class 大小表 */
static const size_t size_class_table[STACK_POOL_MT_NUM_CLASSES] = {
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
int stack_pool_mt_get_class_index(size_t size) {
    for (int i = 0; i < STACK_POOL_MT_NUM_CLASSES; i++) {
        if (size <= size_class_table[i]) {
            return i;
        }
    }
    return -1;  /* 超出范围 */
}

/* 获取 size class 大小 */
size_t stack_pool_mt_get_class_size(int class_idx) {
    if (class_idx < 0 || class_idx >= STACK_POOL_MT_NUM_CLASSES) {
        return 0;
    }
    return size_class_table[class_idx];
}

/* 初始化全局缓存 */
int stack_pool_global_cache_init(void) {
    if (g_global_cache_initialized) {
        return 0;
    }

    for (int i = 0; i < STACK_POOL_MT_NUM_CLASSES; i++) {
        g_global_cache.freelists[i] = NULL;
        g_global_cache.counts[i] = 0;
        g_global_cache.limits[i] = 64;  /* 全局缓存上限较小 */
        pthread_mutex_init(&g_global_cache.locks[i], NULL);
    }

    g_global_cache_initialized = true;
    return 0;
}

/* 销毁全局缓存 */
void stack_pool_global_cache_destroy(void) {
    if (!g_global_cache_initialized) {
        return;
    }

    for (int i = 0; i < STACK_POOL_MT_NUM_CLASSES; i++) {
        pthread_mutex_lock(&g_global_cache.locks[i]);
        stack_node_mt_t *node = g_global_cache.freelists[i];
        while (node) {
            stack_node_mt_t *next = node->next;
            free_stack_mmap(node->stack_top, node->size);
            node = next;
        }
        pthread_mutex_unlock(&g_global_cache.locks[i]);
        pthread_mutex_destroy(&g_global_cache.locks[i]);
    }

    g_global_cache_initialized = false;
}

/* 向全局缓存推送栈 */
void stack_pool_global_cache_push(void *stack_top, size_t size, uint32_t p_id) {
    if (!g_global_cache_initialized || !stack_top) {
        return;
    }

    int class_idx = stack_pool_mt_get_class_index(size);
    if (class_idx < 0) {
        free_stack_mmap(stack_top, size);
        return;
    }

    pthread_mutex_lock(&g_global_cache.locks[class_idx]);

    /* 缓存已满，直接释放 */
    if (g_global_cache.counts[class_idx] >= g_global_cache.limits[class_idx]) {
        pthread_mutex_unlock(&g_global_cache.locks[class_idx]);
        free_stack_mmap(stack_top, size);
        return;
    }

    /* 将节点嵌入栈空间底部 */
    size_t page_size = get_page_size();
    size_t actual_size = size_class_table[class_idx];
    void *stack_base = (void*)((uintptr_t)stack_top - actual_size - page_size);
    stack_node_mt_t *node = (stack_node_mt_t*)((uintptr_t)stack_base + page_size);

    node->stack_top = stack_top;
    node->size = actual_size;
    node->owner_p_id = p_id;
    node->next = g_global_cache.freelists[class_idx];
    g_global_cache.freelists[class_idx] = node;
    g_global_cache.counts[class_idx]++;

    pthread_mutex_unlock(&g_global_cache.locks[class_idx]);
}

/* 从全局缓存弹出栈 */
stack_node_mt_t *stack_pool_global_cache_pop(int class_idx) {
    if (!g_global_cache_initialized || class_idx < 0 || class_idx >= STACK_POOL_MT_NUM_CLASSES) {
        return NULL;
    }

    pthread_mutex_lock(&g_global_cache.locks[class_idx]);

    stack_node_mt_t *node = g_global_cache.freelists[class_idx];
    if (node) {
        g_global_cache.freelists[class_idx] = node->next;
        g_global_cache.counts[class_idx]--;
    }

    pthread_mutex_unlock(&g_global_cache.locks[class_idx]);
    return node;
}

/* 创建 Per-P 栈池 */
stack_pool_per_p_t *stack_pool_per_p_create(uint32_t p_id) {
    stack_pool_per_p_t *pool = calloc(1, sizeof(stack_pool_per_p_t));
    if (!pool) {
        return NULL;
    }

    /* 初始化 size classes */
    for (int i = 0; i < STACK_POOL_MT_NUM_CLASSES; i++) {
        pool->sizes[i] = size_class_table[i];
        pool->limits[i] = STACK_POOL_MT_CLASS_LIMIT;
        pool->freelists[i] = NULL;
        pool->counts[i] = 0;
    }

    /* 默认使用栈顶 1KB 清零模式 */
    pool->zero_mode = STACK_ZERO_TOP_1K;
    pool->p_id = p_id;

    /* 初始化统计 */
    pool->total_allocs = 0;
    pool->total_frees = 0;
    pool->pool_hits = 0;
    pool->pool_misses = 0;

    return pool;
}

/* 销毁 Per-P 栈池 */
void stack_pool_per_p_destroy(stack_pool_per_p_t *pool) {
    if (!pool) {
        return;
    }

    /* 释放所有空闲栈 */
    for (int i = 0; i < STACK_POOL_MT_NUM_CLASSES; i++) {
        stack_node_mt_t *node = pool->freelists[i];
        while (node) {
            stack_node_mt_t *next = node->next;
            free_stack_mmap(node->stack_top, node->size);
            node = next;
        }
    }

    free(pool);
}

/* 从 Per-P 栈池分配 */
void *stack_pool_per_p_alloc(stack_pool_per_p_t *pool, size_t size) {
    if (!pool) {
        return NULL;
    }

    pool->total_allocs++;

    int class_idx = stack_pool_mt_get_class_index(size);

    /* 超出池范围，直接 mmap */
    if (class_idx < 0) {
        pool->pool_misses++;
        return alloc_stack_mmap(size);
    }

    /* 尝试从本地空闲链表获取 */
    stack_node_mt_t *node = pool->freelists[class_idx];
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

    /* 尝试从全局缓存获取 */
    node = stack_pool_global_cache_pop(class_idx);
    if (node) {
        pool->pool_hits++;
        zero_stack(node->stack_top, node->size, pool->zero_mode);
        return node->stack_top;
    }

    /* 空闲链表为空，分配新栈 */
    pool->pool_misses++;
    return alloc_stack_mmap(pool->sizes[class_idx]);
}

/* 释放栈到 Per-P 栈池 */
void stack_pool_per_p_free(stack_pool_per_p_t *pool, void *stack_top, size_t size) {
    if (!pool || !stack_top) {
        return;
    }

    pool->total_frees++;

    int class_idx = stack_pool_mt_get_class_index(size);

    /* 超出池范围，直接 munmap */
    if (class_idx < 0) {
        free_stack_mmap(stack_top, size);
        return;
    }

    /* 检查栈所有权 */
    size_t page_size = get_page_size();
    size_t actual_size = size_class_table[class_idx];
    void *stack_base = (void*)((uintptr_t)stack_top - actual_size - page_size);
    stack_node_mt_t *existing_node = (stack_node_mt_t*)((uintptr_t)stack_base + page_size);

    /* 如果栈不属于当前 P，放入全局缓存 */
    if (existing_node->owner_p_id != pool->p_id) {
        stack_pool_global_cache_push(stack_top, size, existing_node->owner_p_id);
        return;
    }

    /* 本地池已满，放入全局缓存 */
    if (pool->counts[class_idx] >= pool->limits[class_idx]) {
        stack_pool_global_cache_push(stack_top, size, pool->p_id);
        return;
    }

    /* 将节点嵌入栈空间底部 */
    stack_node_mt_t *node = existing_node;
    node->stack_top = stack_top;
    node->size = actual_size;
    node->owner_p_id = pool->p_id;
    node->next = pool->freelists[class_idx];
    pool->freelists[class_idx] = node;
    pool->counts[class_idx]++;
}

/* 栈使用率检测 API - 委托给公共函数 */
size_t stack_pool_mt_get_usage(void *stack_top, size_t size, void *current_sp) {
    return get_usage(stack_top, size, current_sp);
}

/* 获取统计信息 */
void stack_pool_per_p_get_stats(stack_pool_per_p_t *pool,
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