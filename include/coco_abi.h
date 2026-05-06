/**
 * coco_abi.h - ABI 检测和运行时信息
 *
 * 提供编译时和运行时的平台/架构检测，
 * 支持 macOS Universal Binary 场景。
 */

#ifndef COCO_ABI_H
#define COCO_ABI_H

#include <stdint.h>

/* 编译时平台检测 */
#if defined(__linux__)
    #define COCO_PLATFORM_LINUX    1
    #define COCO_PLATFORM_NAME     "Linux"
#elif defined(__APPLE__)
    #define COCO_PLATFORM_MACOS    1
    #define COCO_PLATFORM_NAME     "macOS"
#elif defined(_WIN32)
    #define COCO_PLATFORM_WINDOWS  1
    #define COCO_PLATFORM_NAME     "Windows"
#else
    #define COCO_PLATFORM_UNKNOWN  1
    #define COCO_PLATFORM_NAME     "Unknown"
#endif

/* 编译时架构检测 */
#if defined(__x86_64__) || defined(_M_X64)
    #define COCO_ARCH_X86_64       1
    #define COCO_ARCH_NAME         "x86-64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define COCO_ARCH_ARM64        1
    #define COCO_ARCH_NAME         "ARM64"
#else
    #define COCO_ARCH_UNKNOWN      1
    #define COCO_ARCH_NAME         "Unknown"
#endif

/* ABI 类型 */
typedef enum coco_abi_type {
    COCO_ABI_SYSTEM_V_AMD64,    /* Linux/macOS x86-64 */
    COCO_ABI_MICROSOFT_X64,     /* Windows x86-64 */
    COCO_ABI_AAPCS64,           /* Linux/macOS/Windows ARM64 */
    COCO_ABI_UNKNOWN
} coco_abi_type_t;

/* ABI 信息结构 */
typedef struct coco_abi_info {
    int platform;           /* COCO_PLATFORM_* */
    int arch;               /* COCO_ARCH_* */
    coco_abi_type_t abi;    /* ABI 类型 */
    int has_fpu;            /* 是否需要保存 FPU 寄存器 */
    int ctx_size;           /* 上下文结构总大小 */
    int ctx_asm_size;       /* 汇编保存的大小 */
    const char *platform_name;
    const char *arch_name;
    const char *abi_name;
} coco_abi_info_t;

/**
 * coco_get_abi_info - 获取当前 ABI 信息
 * @return: 指向 ABI 信息结构的指针（静态分配）
 */
const coco_abi_info_t* coco_get_abi_info(void);

/**
 * coco_get_abi_name - 获取 ABI 名称字符串
 * @return: ABI 名称（如 "System V AMD64", "Microsoft x64", "AAPCS64"）
 */
const char* coco_get_abi_name(void);

/* macOS 特有：运行时架构检测 */

#if defined(COCO_PLATFORM_MACOS)

/**
 * coco_runtime_is_arm64 - 检测当前是否运行在 ARM64 架构上
 *
 * 用于 Universal Binary 场景，检测当前进程的实际架构。
 * 在原生 ARM64 进程中返回 1，在 x86-64 进程中返回 0。
 *
 * @return: 1 = ARM64, 0 = x86-64
 */
int coco_runtime_is_arm64(void);

/**
 * coco_runtime_is_rosetta - 检测当前是否运行在 Rosetta 2 转译模式下
 *
 * Rosetta 2 是 Apple Silicon 上运行 x86-64 代码的转译层。
 * 在 Rosetta 下返回 1，原生执行返回 0。
 *
 * @return: 1 = Rosetta 转译, 0 = 原生执行
 */
int coco_runtime_is_rosetta(void);

#endif /* COCO_PLATFORM_MACOS */

#endif /* COCO_ABI_H */