/**
 * test_stack.c - 栈管理单元测试
 */

#include "../src/coco_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define STACK_SIZE (64 * 1024)  /* 64KB */

void test_stack_alloc(void) {
    printf("test_stack_alloc: ");

    void *stack_top = coco_stack_alloc(STACK_SIZE);
    assert(stack_top != NULL);

    /* 验证栈地址有效 */
    uintptr_t base = (uintptr_t)stack_top - STACK_SIZE - 4096;
    printf("allocated at %p, base at %p\n", stack_top, (void*)base);

    /* 写入测试（访问可用栈区域） */
    char *p = (char*)stack_top - 1;
    *p = 'A';
    assert(*p == 'A');

    coco_stack_free(stack_top, STACK_SIZE);
    printf("PASS\n");
}

void test_stack_multiple(void) {
    printf("test_stack_multiple: ");

    /* 分配多个栈 */
    void *stacks[10];
    for (int i = 0; i < 10; i++) {
        stacks[i] = coco_stack_alloc(STACK_SIZE);
        assert(stacks[i] != NULL);
    }

    /* 释放所有栈 */
    for (int i = 0; i < 10; i++) {
        coco_stack_free(stacks[i], STACK_SIZE);
    }

    printf("PASS (allocated and freed 10 stacks)\n");
}

int main(void) {
    printf("=== Stack Tests ===\n");
    test_stack_alloc();
    test_stack_multiple();
    printf("All tests passed!\n");
    return 0;
}