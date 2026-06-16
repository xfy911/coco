/**
 * stack_pool_multi.c - 多尺寸栈池原型实现 (Phase 0 验证)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * 单线程设计，用于验证栈池扩展可行性
 */

#include "stack_common.h"
#include "stack_pool_multi.h"
#include "../coco_internal.h"
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

/**
 * 获取 size class 索引（根据请求大小向上取整）
 */
int stack_pool_multi_get_class_index(size_t size) {
    for (int i = 0; i < STACK_POOL_MULTI_NUM_CLASSES; i++) {
        if (size <= size_class_table[i]) {
            return i;
        }
    }
    return -1;  /* 超出范围 */
}

/**
 * 获取 size class 大小
 */
size_t stack_pool_multi_get_class_size(int class_idx) {
    if (class_idx < 0 || class_idx >= STACK_POOL_MULTI_NUM_CLASSES) {
        return 0;
    }
    return size_class_table[class_idx];
}

/**
 * 线程局部缓存 - 每个线程缓存 1 个常用 size class 的栈
 */
static _Thread_local struct {
    void *cache[STACK_POOL_MULTI_NUM_CLASSES];      /* 缓存的栈顶指针 */
} tl_stack_cache;

/* 快速路径释放计数（在 get_stats / flush 时合并到全局计数） */
static _Thread_local uint64_t tl_fast_frees = 0;

/**
 * 将线程局部缓存的栈刷新回池（带锁）或释放
 */
static void flush_tl_cache_to_pool(stack_pool_multi_t *pool, bool free_all) {
    if (!pool) return;

    /* 合并快速路径释放计数到全局统计 */
    if (tl_fast_frees > 0) {
        pthread_mutex_lock(&pool->lock);
        pool->total_frees += tl_fast_frees;
        pthread_mutex_unlock(&pool->lock);
        tl_fast_frees = 0;
    }

    for (int i = 0; i < STACK_POOL_MULTI_NUM_CLASSES; i++) {
        void *stack = tl_stack_cache.cache[i];
        if (!stack) continue;

        if (free_all) {
            free_stack_mmap(stack, pool->sizes[i]);
        } else {
            /* 尝试放回池中（带锁） */
            pthread_mutex_lock(&pool->lock);
            if (pool->counts[i] < pool->limits[i]) {
                size_t page_size = get_page_size();
                size_t actual_size = pool->sizes[i];
                void *stack_base = (void*)((uintptr_t)stack - actual_size - page_size);
                stack_node_multi_t *node = (stack_node_multi_t*)((uintptr_t)stack_base + page_size);
                node->stack_top = stack;
                node->size = actual_size;
                node->next = pool->freelists[i];
                pool->freelists[i] = node;
                pool->counts[i]++;
                pthread_mutex_unlock(&pool->lock);
            } else {
                pthread_mutex_unlock(&pool->lock);
                free_stack_mmap(stack, pool->sizes[i]);
            }
        }
        tl_stack_cache.cache[i] = NULL;
    }
}

/**
 * 快速分配路径（无锁，线程局部缓存）
 */
static void *stack_pool_multi_alloc_fast(stack_pool_multi_t *pool, size_t size) {
    int class_idx = stack_pool_multi_get_class_index(size);
    if (class_idx >= 0 && tl_stack_cache.cache[class_idx]) {
        void *stack = tl_stack_cache.cache[class_idx];
        tl_stack_cache.cache[class_idx] = NULL;
        zero_stack(stack, pool->sizes[class_idx], pool->zero_mode);
        return stack;
    }
    return NULL;
}

/**
 * 慢速释放路径（带锁，回收到全局池或直接 munmap）
 */
static void stack_pool_multi_free_slow(stack_pool_multi_t *pool, void *stack_top, size_t size) {
    coco_preempt_block_signal();
    pthread_mutex_lock(&pool->lock);

    pool->total_frees++;

    int class_idx = stack_pool_multi_get_class_index(size);

    /* 超出池范围，直接 munmap */
    if (class_idx < 0) {
        pthread_mutex_unlock(&pool->lock);
        coco_preempt_unblock_signal();
        free_stack_mmap(stack_top, size);
        return;
    }

    /* 池已满，直接 munmap */
    if (pool->counts[class_idx] >= pool->limits[class_idx]) {
        pthread_mutex_unlock(&pool->lock);
        coco_preempt_unblock_signal();
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

    pthread_mutex_unlock(&pool->lock);
    coco_preempt_unblock_signal();
}

/**
 * 快速释放路径（无锁，缓存在本地）
 */
static void stack_pool_multi_free_fast(stack_pool_multi_t *pool, void *stack_top, size_t size) {
    int class_idx = stack_pool_multi_get_class_index(size);
    if (class_idx >= 0 && !tl_stack_cache.cache[class_idx]) {
        tl_stack_cache.cache[class_idx] = stack_top;
        tl_fast_frees++;
        return;
    }
    /* 缓存槽已满或不支持的 size class，走慢路径 */
    stack_pool_multi_free_slow(pool, stack_top, size);
}

/**
 * 创建多尺寸栈池
 */
stack_pool_multi_t *stack_pool_multi_create(void) {
    stack_pool_multi_t *pool = calloc(1, sizeof(stack_pool_multi_t));
    if (!pool) {
        return NULL;
    }

    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        free(pool);
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

/**
 * 销毁栈池并释放所有资源
 */
void stack_pool_multi_destroy(stack_pool_multi_t *pool) {
    if (!pool) {
        return;
    }

    /* 释放当前线程缓存的栈 */
    flush_tl_cache_to_pool(pool, true);

    /* 释放所有空闲栈 */
    for (int i = 0; i < STACK_POOL_MULTI_NUM_CLASSES; i++) {
        stack_node_multi_t *node = pool->freelists[i];
        while (node) {
            stack_node_multi_t *next = node->next;
            free_stack_mmap(node->stack_top, node->size);
            node = next;
        }
    }

    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

/**
 * 从栈池分配栈（优先走快速路径）
 */
void *stack_pool_multi_alloc(stack_pool_multi_t *pool, size_t size) {
    if (!pool) {
        return NULL;
    }

    /* 快速路径：线程局部缓存 */
    void *fast = stack_pool_multi_alloc_fast(pool, size);
    if (fast) {
        coco_preempt_block_signal();
        pthread_mutex_lock(&pool->lock);
        pool->total_allocs++;
        pool->pool_hits++;
        pthread_mutex_unlock(&pool->lock);
        coco_preempt_unblock_signal();
        return fast;
    }

    coco_preempt_block_signal();
    pthread_mutex_lock(&pool->lock);

    pool->total_allocs++;

    int class_idx = stack_pool_multi_get_class_index(size);
    void *result = NULL;

    /* 超出池范围，直接 mmap */
    if (class_idx < 0) {
        pool->pool_misses++;
        result = alloc_stack_mmap(size);
        goto done;
    }

    /* 尝试从空闲链表获取 */
    stack_node_multi_t *node = pool->freelists[class_idx];
    if (node) {
        pool->freelists[class_idx] = node->next;
        pool->counts[class_idx]--;
        pool->pool_hits++;

        result = node->stack_top;
        size_t actual_size = node->size;

        /* 选择性清零栈 */
        zero_stack(result, actual_size, pool->zero_mode);
        goto done;
    }

    /* 空闲链表为空，分配新栈 */
    pool->pool_misses++;
    result = alloc_stack_mmap(pool->sizes[class_idx]);

done:
    pthread_mutex_unlock(&pool->lock);
    coco_preempt_unblock_signal();
    return result;
}

/**
 * 释放栈回栈池（优先走快速路径）
 */
void stack_pool_multi_free(stack_pool_multi_t *pool, void *stack_top, size_t size) {
    if (!pool || !stack_top) {
        return;
    }

    stack_pool_multi_free_fast(pool, stack_top, size);
}

/**
 * 获取栈使用率
 */
size_t stack_pool_multi_get_usage(void *stack_top, size_t size, void *current_sp) {
    return get_usage(stack_top, size, current_sp);
}

/**
 * 获取栈池统计信息
 */
void stack_pool_multi_get_stats(stack_pool_multi_t *pool,
                                uint64_t *total_allocs,
                                uint64_t *total_frees,
                                uint64_t *pool_hits,
                                uint64_t *pool_misses) {
    if (!pool) {
        return;
    }

    /* 刷新当前线程缓存以确保统计准确 */
    flush_tl_cache_to_pool(pool, false);

    if (total_allocs) *total_allocs = pool->total_allocs;
    if (total_frees) *total_frees = pool->total_frees;
    if (pool_hits) *pool_hits = pool->pool_hits;
    if (pool_misses) *pool_misses = pool->pool_misses;
}