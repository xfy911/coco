/**
 * test_ctx_arm64.c - ARM64 上下文切换测试
 *
 * 测试 ARM64 平台的上下文切换 ABI 合规性。
 */

#include "test_ctx_common.h"
#include "coco_internal.h"
#include "coco_abi.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

/* 测试协程上下文 */
static coco_ctx_t ctx_main, ctx_test;
static volatile int test_passed = 0;
static void *test_stack = NULL;
static size_t test_stack_size = 64 * 1024;

/* 测试入口函数 - 简单切换 */
static void test_coro_entry(void *arg) {
    (void)arg;
    test_passed = 1;
    coco_ctx_switch(&ctx_test, &ctx_main);
}

/* 分配测试栈 */
static void* alloc_test_stack(size_t size) {
    void *stack = malloc(size);
    if (stack) {
        memset(stack, 0, size);
    }
    return stack;
}

/* 释放测试栈 */
static void free_test_stack(void *stack) {
    free(stack);
}

int test_ctx_size(void) {
    const coco_abi_info_t *abi = coco_get_abi_info();

    TEST_ASSERT(sizeof(coco_ctx_t) == (size_t)abi->ctx_size,
                "coco_ctx_t size mismatch");

    TEST_ASSERT(COCO_CTX_SIZE == abi->ctx_size,
                "COCO_CTX_SIZE macro mismatch");

    TEST_ASSERT(COCO_CTX_ASM_SIZE == abi->ctx_asm_size,
                "COCO_CTX_ASM_SIZE macro mismatch");

    printf("  ctx_size: %d bytes, asm_size: %d bytes\n",
           abi->ctx_size, abi->ctx_asm_size);

    return 0;
}

int test_abi_info(void) {
    const coco_abi_info_t *abi = coco_get_abi_info();

    TEST_ASSERT(abi != NULL, "abi_info is NULL");

#if defined(__aarch64__)
    TEST_ASSERT(abi->arch == COCO_ARCH_ARM64, "arch should be ARM64");
    TEST_ASSERT(abi->abi == COCO_ABI_AAPCS64, "abi should be AAPCS64");
    TEST_ASSERT(abi->has_fpu == 1, "ARM64 should have FPU");
#endif

    printf("  platform: %s, arch: %s, abi: %s\n",
           abi->platform_name, abi->arch_name, abi->abi_name);

    return 0;
}

/**
 * test_register_integrity - 测试整数寄存器完整性
 *
 * 测试方法：直接验证 coco_ctx_save 的正确性
 * 1. 设置寄存器值
 * 2. 调用 coco_ctx_save 保存到结构体
 * 3. 验证结构体中的值正确
 */
int test_register_integrity(void) {
    coco_ctx_t ctx;

    /* 使用内联汇编设置寄存器并调用 save */
    __asm__ volatile (
        "mov x19, %0\n"
        "mov x20, %1\n"
        "mov x21, %2\n"
        "mov x22, %3\n"
        "mov x23, %4\n"
        "mov x24, %5\n"
        "mov x25, %6\n"
        "mov x26, %7\n"
        "mov x27, %8\n"
        "mov x28, %9\n"
        "mov x0, %10\n"
        "bl _coco_ctx_save\n"
        :
        : "r"(PATTERN_A), "r"(PATTERN_B),
          "r"(0x1111111111111111ULL), "r"(0x2222222222222222ULL),
          "r"(0x3333333333333333ULL), "r"(0x4444444444444444ULL),
          "r"(0x5555555555555555ULL), "r"(0x6666666666666666ULL),
          "r"(0x7777777777777777ULL), "r"(0x8888888888888888ULL),
          "r"(&ctx)
        : "x0", "x19", "x20", "x21", "x22", "x23", "x24",
          "x25", "x26", "x27", "x28", "x30", "memory"
    );

    TEST_ASSERT(ctx.x19 == (void*)PATTERN_A, "x19 not saved correctly");
    TEST_ASSERT(ctx.x20 == (void*)PATTERN_B, "x20 not saved correctly");
    TEST_ASSERT(ctx.x21 == (void*)0x1111111111111111ULL, "x21 not saved correctly");
    TEST_ASSERT(ctx.x22 == (void*)0x2222222222222222ULL, "x22 not saved correctly");
    TEST_ASSERT(ctx.x23 == (void*)0x3333333333333333ULL, "x23 not saved correctly");
    TEST_ASSERT(ctx.x24 == (void*)0x4444444444444444ULL, "x24 not saved correctly");
    TEST_ASSERT(ctx.x25 == (void*)0x5555555555555555ULL, "x25 not saved correctly");
    TEST_ASSERT(ctx.x26 == (void*)0x6666666666666666ULL, "x26 not saved correctly");
    TEST_ASSERT(ctx.x27 == (void*)0x7777777777777777ULL, "x27 not saved correctly");
    TEST_ASSERT(ctx.x28 == (void*)0x8888888888888888ULL, "x28 not saved correctly");

    return 0;
}

/**
 * test_fpu_preservation - 测试浮点寄存器保存
 */
int test_fpu_preservation(void) {
    coco_ctx_t ctx;

    double d8_val = 1.1, d9_val = 2.2, d10_val = 3.3, d11_val = 4.4;
    double d12_val = 5.5, d13_val = 6.6, d14_val = 7.7, d15_val = 8.8;

    __asm__ volatile (
        "ldr d8, %0\n"
        "ldr d9, %1\n"
        "ldr d10, %2\n"
        "ldr d11, %3\n"
        "ldr d12, %4\n"
        "ldr d13, %5\n"
        "ldr d14, %6\n"
        "ldr d15, %7\n"
        "mov x0, %8\n"
        "bl _coco_ctx_save\n"
        :
        : "m"(d8_val), "m"(d9_val), "m"(d10_val), "m"(d11_val),
          "m"(d12_val), "m"(d13_val), "m"(d14_val), "m"(d15_val),
          "r"(&ctx)
        : "x0", "x30", "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15", "memory"
    );

    TEST_ASSERT(ctx.d8 == 1.1, "d8 not saved correctly");
    TEST_ASSERT(ctx.d9 == 2.2, "d9 not saved correctly");
    TEST_ASSERT(ctx.d10 == 3.3, "d10 not saved correctly");
    TEST_ASSERT(ctx.d11 == 4.4, "d11 not saved correctly");
    TEST_ASSERT(ctx.d12 == 5.5, "d12 not saved correctly");
    TEST_ASSERT(ctx.d13 == 6.6, "d13 not saved correctly");
    TEST_ASSERT(ctx.d14 == 7.7, "d14 not saved correctly");
    TEST_ASSERT(ctx.d15 == 8.8, "d15 not saved correctly");

    return 0;
}

int test_switch_performance(void) {
    test_stack = alloc_test_stack(test_stack_size);
    TEST_ASSERT(test_stack != NULL, "failed to allocate test stack");

    void *stack_top = (char*)test_stack + test_stack_size;

    memset(&ctx_test, 0, sizeof(ctx_test));
    memset(&ctx_main, 0, sizeof(ctx_main));

    coco_ctx_init(&ctx_test, stack_top, test_coro_entry, NULL);

    const int iterations = 1000000;
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        coco_ctx_switch(&ctx_main, &ctx_test);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long long total_ns = (end.tv_sec - start.tv_sec) * 1000000000LL +
                         (end.tv_nsec - start.tv_nsec);
    double avg_ns = (double)total_ns / (iterations * 2);

    printf("  avg switch time: %.2f ns\n", avg_ns);

    TEST_ASSERT(avg_ns < 100.0, "switch performance exceeds 100ns target");

    free_test_stack(test_stack);

    return 0;
}

int main(void) {
    int failed = 0;

    printf("=== ARM64 Context Switch Tests ===\n\n");

    printf("test_ctx_size...\n");
    failed += test_ctx_size();

    printf("test_abi_info...\n");
    failed += test_abi_info();

    printf("test_register_integrity...\n");
    failed += test_register_integrity();

    printf("test_fpu_preservation...\n");
    failed += test_fpu_preservation();

    printf("test_switch_performance...\n");
    failed += test_switch_performance();

    printf("\n=== Results: %d/%d tests passed ===\n",
           5 - failed, 5);

    return failed;
}
