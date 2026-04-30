// test_stack_map.c - Simple test for stack map generation
#include <stdio.h>

__attribute__((noinline))
void test_func(int a, int b) {
    int local1 = a + 1;
    int local2 = b + 2;
    int *ptr = &local1;

    printf("local1=%d, local2=%d, ptr=%p\n", local1, local2, ptr);
}

int main() {
    test_func(10, 20);
    return 0;
}