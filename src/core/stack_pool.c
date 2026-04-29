/**
 * stack_pool.c - 栈池实现
 *
 * 单线程设计：栈池仅限单调度器使用，无需同步
 */

#include "stack_pool.h"
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

/* 获取 size class 索引 */
static int get_size_class_index(size_t size) {
    if (size <= STACK_SIZE_16K) return 0;
    if (size <= STACK_SIZE_32K) return 1;
    if (size <= STACK_SIZE_64K) return 2;
    if (size <= STACK_SIZE_128K) return 3;
    return -1;  /* 超出范围，不使用池 */
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

    /* 设置 guard page */
    if (mprotect(stack_base, page_size, PROT_NONE) != 0) {
        munmap(stack_base, total_size);
        return NULL;
    }

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
stack_pool_t *stack_pool_create(void) {
    stack_pool_t *pool = calloc(1, sizeof(stack_pool_t));
    if (!pool) {
        return NULL;
    }

    /* 初始化 size classes */
    pool->sizes[0] = STACK_SIZE_16K;
    pool->sizes[1] = STACK_SIZE_32K;
    pool->sizes[2] = STACK_SIZE_64K;
    pool->sizes[3] = STACK_SIZE_128K;

    /* 设置上限 */
    for (int i = 0; i < STACK_POOL_NUM_CLASSES; i++) {
        pool->limits[i] = STACK_POOL_CLASS_LIMIT;
        pool->freelists[i] = NULL;
        pool->counts[i] = 0;
    }

    /* 默认使用栈顶 1KB 清零模式 */
    pool->zero_mode = STACK_ZERO_TOP_1K;

    return pool;
}

/* 销毁栈池 */
void stack_pool_destroy(stack_pool_t *pool) {
    if (!pool) {
        return;
    }

    /* 释放所有空闲栈（节点嵌入在栈空间中，直接 munmap 即可） */
    for (int i = 0; i < STACK_POOL_NUM_CLASSES; i++) {
        stack_node_t *node = pool->freelists[i];
        while (node) {
            stack_node_t *next = node->next;
            free_stack_mmap(node->stack_top, node->size);
            /* 节点嵌入在栈空间中，无需 free */
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
        /* 仅清零栈顶 1KB（协程入口使用区域） */
        memset((void*)((uintptr_t)stack_top - 1024), 0, 1024);
    } else {
        /* 清零全部 */
        void *stack_base = (void*)((uintptr_t)stack_top - size);
        memset(stack_base, 0, size);
    }
}

/* 从栈池分配 */
void *stack_pool_alloc(stack_pool_t *pool, size_t size) {
    if (!pool) {
        return NULL;
    }

    int class_idx = get_size_class_index(size);

    /* 超出池范围，直接 mmap */
    if (class_idx < 0) {
        return alloc_stack_mmap(size);
    }

    /* 尝试从空闲链表获取 */
    stack_node_t *node = pool->freelists[class_idx];
    if (node) {
        pool->freelists[class_idx] = node->next;
        pool->counts[class_idx]--;

        void *stack_top = node->stack_top;
        size_t actual_size = node->size;

        /* 节点嵌入在栈空间中，无需 free */

        /* 选择性清零栈 */
        zero_stack(stack_top, actual_size, pool->zero_mode);

        return stack_top;
    }

    /* 空闲链表为空，分配新栈 */
    return alloc_stack_mmap(pool->sizes[class_idx]);
}

/* 释放栈到池 */
void stack_pool_free(stack_pool_t *pool, void *stack_top, size_t size) {
    if (!pool || !stack_top) {
        return;
    }

    int class_idx = get_size_class_index(size);

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
    /* 栈布局: [guard page][stack space] */
    /* stack_top 指向栈空间末尾，stack_base = stack_top - stack_size - page_size */
    /* 节点放在 stack_base + page_size（即栈空间起始位置） */
    size_t page_size = get_page_size();
    size_t actual_size = pool->sizes[class_idx];
    void *stack_base = (void*)((uintptr_t)stack_top - actual_size - page_size);
    stack_node_t *node = (stack_node_t*)((uintptr_t)stack_base + page_size);

    node->stack_top = stack_top;
    node->size = actual_size;
    node->next = pool->freelists[class_idx];
    pool->freelists[class_idx] = node;
    pool->counts[class_idx]++;
}
