/**
 * stack.c - 栈分配 + guard page
 *
 * 使用 mmap 分配栈内存，并设置 guard page 检测栈溢出。
 */

#include "coco_internal.h"
#include <stdlib.h>
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

/**
 * coco_stack_alloc - 分配栈内存 + guard page
 *
 * @param size 栈大小（不含 guard page）
 * @return 栈顶指针（用于 ctx_init），失败返回 NULL
 *
 * 栈布局（从高地址到低地址）：
 *   +-------------------+  <- stack_base + size + page_size (返回值 = stack_top)
 *   |   usable stack    |  用户可用栈空间
 *   +-------------------+  <- stack_base + page_size (guard page 边界)
 *   |   guard page      |  受保护页，访问触发 SIGSEGV
 *   +-------------------+  <- stack_base
 */
void *coco_stack_alloc(size_t size) {
    size_t page_size = get_page_size();

    /* 确保 size 是页大小的倍数 */
    size = (size + page_size - 1) & ~(page_size - 1);

    /* 总分配大小 = 栈 + guard page */
    size_t total_size = size + page_size;

    /* 使用 mmap 分配匿名内存 */
    void *stack_base = mmap(
        NULL,                   /* 地址由系统决定 */
        total_size,             /* 总大小 */
        PROT_READ | PROT_WRITE, /* 可读写 */
        MAP_PRIVATE | MAP_ANONYMOUS, /* 私有匿名映射 */
        -1,                     /* 无文件描述符 */
        0                       /* 偏移 */
    );

    if (stack_base == MAP_FAILED) {
        return NULL;
    }

    /* 设置 guard page 为不可访问 */
    if (mprotect(stack_base, page_size, PROT_NONE) != 0) {
        munmap(stack_base, total_size);
        return NULL;
    }

    /* 返回栈顶地址 */
    return (void*)((uintptr_t)stack_base + total_size);
}

/**
 * coco_stack_free - 释放栈内存
 *
 * @param stack 栈顶指针（由 coco_stack_alloc 返回）
 * @param size 栈大小（不含 guard page）
 */
void coco_stack_free(void *stack, size_t size) {
    if (stack == NULL) {
        return;
    }

    size_t page_size = get_page_size();
    size = (size + page_size - 1) & ~(page_size - 1);
    size_t total_size = size + page_size;

    /* 计算栈基址 */
    void *stack_base = (void*)((uintptr_t)stack - total_size);

    munmap(stack_base, total_size);
}