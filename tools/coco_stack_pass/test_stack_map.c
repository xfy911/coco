// test_stack_map.c - Multi-function test for stack map generation
// US-218: Test multiple functions with different stack patterns
#include <stdio.h>

// Simple function with local variables
__attribute__((noinline))
void func_a(int x) {
    int local = x * 2;
    printf("a: %d\n", local);
}

// Function with array on stack
__attribute__((noinline))
void func_b(int y) {
    int arr[10];
    arr[0] = y;
    printf("b: %d\n", arr[0]);
}

// Function with pointer parameter
__attribute__((noinline))
int func_c(int* ptr) {
    return *ptr + 1;
}

// Function with larger stack frame
__attribute__((noinline))
void func_d(void) {
    char buffer[256];
    for (int i = 0; i < 10; i++) {
        buffer[i] = 'A' + i;
    }
    buffer[10] = '\0';
    printf("d: %s\n", buffer);
}

int main() {
    func_a(1);
    func_b(2);
    int val = 3;
    printf("c: %d\n", func_c(&val));
    func_d();
    return 0;
}