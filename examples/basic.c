/**
 * basic.c - 基础用法示例
 */

#include "coco.h"
#include <stdio.h>

void coro_func(void *arg) {
    int *counter = (int*)arg;
    printf("Coroutine started, counter = %d\n", *counter);

    for (int i = 0; i < 3; i++) {
        (*counter)++;
        printf("Coroutine iteration %d, counter = %d\n", i, *counter);
        coco_yield();
    }

    printf("Coroutine finished\n");
}

int main(void) {
    printf("=== Basic Coroutine Example ===\n");
    printf("TODO: Implement after coroutine core\n");
    return 0;
}