/**
 * stack_common.c - 共享栈基础设施实现
 *
 * 提供栈分配、释放、清零和使用率检测的公共函数实现，
 * 供 stack.c / stack_pool.c / stack_pool_multi.c / stack_pool_mt.c 共用。
 */

#include "stack_common.h"
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __APPLE__
#define MAP_ANONYMOUS MAP_ANON
#endif

/* 获取系统页大小 */
size_t get_page_size(void) {
    static size_t page_size = 0;
    if (page_size == 0) {
        page_size = sysconf(_SC_PAGESIZE);
    }
    return page_size;
}

/* 分配栈（mmap + guard page） */
void *alloc_stack_mmap(size_t size) {
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
void free_stack_mmap(void *stack_top, size_t size) {
    size_t page_size = get_page_size();
    size = (size + page_size - 1) & ~(page_size - 1);
    size_t total_size = size + page_size;
    void *stack_base = (void*)((uintptr_t)stack_top - total_size);
    munmap(stack_base, total_size);
}

/* 选择性清零栈 */
void zero_stack(void *stack_top, size_t size, stack_zero_mode_t mode) {
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
size_t get_usage(void *stack_top, size_t size, void *current_sp) {
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
