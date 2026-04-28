/**
 * test_cancel.c - 协程取消测试
 */

#include "../include/coco.h"
#include <stdio.h>

int main(void) {
    printf("=== Cancel Tests ===\n");

    /* 测试 1: 取消 NULL 协程 */
    printf("test_cancel_null: ");
    int ret = coco_cancel(NULL);
    if (ret == COCO_ERROR) {
        printf("PASS\n");
    } else {
        printf("FAIL (got %d)\n", ret);
        return 1;
    }

    /* 测试 2: 在非协程上下文调用 coco_cancelled */
    printf("test_cancelled_outside: ");
    int cancelled = coco_cancelled();
    if (cancelled == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL (got %d)\n", cancelled);
        return 1;
    }

    printf("=== All Cancel Tests PASSED ===\n");
    return 0;
}