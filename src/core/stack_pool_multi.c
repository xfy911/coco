/**
 * stack_pool_multi.c - 多尺寸栈池原型实现 (Phase 0 验证)
 *
 * 支持 8 种尺寸: 8KB/16KB/32KB/64KB/128KB/256KB/512KB/1MB
 * 单线程设计，用于验证栈池扩展可行性
 */

#include "stack_pool_multi.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __APPLE__
#define MAP_ANONYMOUS MAP_ANON
#endif

/* 获取系统页大小 */
static size_t get_page_size(void) {
    static size_t page_size = 0;
    if (page_size == 0) {
        page_size = sysconf(_SC_PAGESIZE);
    }
    return page_size;
}

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

/* 分配栈（mmap + guard page） */
static void *alloc_stack_mmap(size_t size) {
    size_t page_size = get_page_size();
    size = (size + page_size - 1) & ~(page_size - 1);
    size_t total_size = size + page_size;

    void *stack_base = mmap(
        NULL,
        total_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

    if (stack_base == MAP_FAILED) {
        return NULL;
    }

    /* 设置 guard page（栈底，防止向下溢出） */
    if (mprotect(stack_base, page_size, PROT_NONE) != 0) {
        munmap(stack_base, total_size);
        return NULL;
    }

    /* 返回栈顶地址 */
    return (void*)((uintptr_t)stack_base + total_size);
}

/* 释放栈（munmap） */
static void free_stack_mmap(void *stack_top, size_t size) {
    size_t page_size = get_page_size();
    size = (size + page_size - 1) & ~(page_size - 1);
    size_t total_size = size + page_size;
    void *stack_base = (void*)((uintptr_t)stack_top - total_size);
    munmap(stack_base, total_size);
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

/* 选择性清零栈 */
static void zero_stack(void *stack_top, size_t size, stack_zero_mode_t mode) {
    if (mode == STACK_ZERO_NONE) {
        return;
    }

    if (mode == STACK_ZERO_TOP_1K) {
        /* 仅清零栈顶 1KB */
        memset((void*)((uintptr_t)stack_top - 1024), 0, 1024);
    } else {
        /* 清零全部 */
        void *stack_base = (void*)((uintptr_t)stack_top - size);
        memset(stack_base, 0, size);
    }
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

/* 栈使用率检测
 *
 * 通过检查栈内容模式估算使用量。
 * 假设未使用的栈区域仍为零（依赖清零模式）。
 *
 * @param stack_top 栈顶地址
 * @param size 栈大小
 * @param current_sp 当前栈指针（可选，用于精确检测）
 * @return 估算的栈使用量（字节）
 */
size_t stack_pool_multi_get_usage(void *stack_top, size_t size, void *current_sp) {
    if (!stack_top || size == 0) {
        return 0;
    }

    size_t page_size = get_page_size();

    /* 如果提供了当前 SP，直接计算 */
    if (current_sp) {
        uintptr_t sp = (uintptr_t)current_sp;
        uintptr_t top = (uintptr_t)stack_top;
        uintptr_t base = top - size;

        /* SP 应在栈范围内 */
        if (sp >= base && sp <= top) {
            return top - sp;
        }
    }

    /* 无 SP 时，通过扫描零区域估算 */
    /* 从栈底向上扫描，找到第一个非零区域 */
    uintptr_t base = (uintptr_t)stack_top - size;
    uintptr_t aligned_base = (base + page_size - 1) & ~(page_size - 1);

    /* 按 64 字节块扫描（加速） */
    const size_t scan_step = 64;
    size_t zero_region = 0;

    for (uintptr_t addr = aligned_base; addr < (uintptr_t)stack_top - scan_step; addr += scan_step) {
        uint64_t *block = (uint64_t*)addr;
        bool is_zero = true;

        /* 检查 8 个 64-bit 值（共 64 字节） */
        for (int i = 0; i < 8; i++) {
            if (block[i] != 0) {
                is_zero = false;
                break;
            }
        }

        if (is_zero) {
            zero_region += scan_step;
        } else {
            /* 找到非零区域，停止扫描 */
            break;
        }
    }

    /* 使用量 = 总大小 - 零区域 */
    size_t estimated_usage = size - zero_region;

    /* 至少返回一个最小值（避免零结果） */
    if (estimated_usage < 64) {
        estimated_usage = 64;
    }

    return estimated_usage;
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