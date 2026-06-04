#include "coco.h"
#include <stdio.h>

static int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

static void deep_recursion(void *arg) {
    (void)arg;
    /* Start with small stack (2KB) — it will grow automatically */
    int result = fib(20);
    printf("dynamic_stack: fib(20) = %d (stack grew as needed)\n", result);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    /* 2KB initial stack triggers dynamic growth */
    coco_create(sched, deep_recursion, NULL, 2048);
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    printf("dynamic_stack: done\n");
    return 0;
}
