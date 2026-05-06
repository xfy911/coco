/**
 * test_ctx_common.h - 上下文测试通用头文件
 */

#ifndef TEST_CTX_COMMON_H
#define TEST_CTX_COMMON_H

#include <stdint.h>
#include <assert.h>

/* 寄存器模式测试值 */
#define PATTERN_A  0xDEADBEEFCAFEBABEULL
#define PATTERN_B  0x0123456789ABCDEFULL
#define PATTERN_FP 3.14159265358979323846

/* 测试函数声明 */

/**
 * test_register_integrity - 测试整数寄存器完整性
 *
 * 验证所有 callee-saved 整数寄存器在上下文切换后保持不变。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_register_integrity(void);

/**
 * test_fpu_preservation - 测试浮点寄存器保存
 *
 * 验证浮点寄存器 (ARM64: d8-d15, Windows x86-64: xmm6-xmm15)
 * 在上下文切换后保持不变。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_fpu_preservation(void);

/**
 * test_stack_pointer - 测试栈指针
 *
 * 验证栈指针在上下文切换后正确恢复。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_stack_pointer(void);

/**
 * test_frame_pointer - 测试帧指针
 *
 * 验证帧指针在上下文切换后正确恢复。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_frame_pointer(void);

/**
 * test_switch_performance - 性能基准测试
 *
 * 测量上下文切换延迟，验证满足性能目标。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_switch_performance(void);

/**
 * test_ctx_size - 测试上下文大小
 *
 * 验证 coco_ctx_t 结构体大小与汇编实现一致。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_ctx_size(void);

/**
 * test_abi_info - 测试 ABI 信息
 *
 * 验证 coco_get_abi_info() 返回正确的平台信息。
 *
 * @return: 0 = PASS, 非0 = FAIL
 */
int test_abi_info(void);

/* 测试辅助宏 */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s at %s:%d\n", msg, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#endif /* TEST_CTX_COMMON_H */