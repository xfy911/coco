/**
 * abi_detect.c - ABI 检测实现
 */

#include "coco_abi.h"

/* 静态 ABI 信息 */
static coco_abi_info_t g_abi_info = {
#if defined(COCO_PLATFORM_LINUX)
    .platform = COCO_PLATFORM_LINUX,
    .platform_name = "Linux",
#elif defined(COCO_PLATFORM_MACOS)
    .platform = COCO_PLATFORM_MACOS,
    .platform_name = "macOS",
#elif defined(COCO_PLATFORM_WINDOWS)
    .platform = COCO_PLATFORM_WINDOWS,
    .platform_name = "Windows",
#else
    .platform = COCO_PLATFORM_UNKNOWN,
    .platform_name = "Unknown",
#endif

#if defined(COCO_ARCH_X86_64)
    .arch = COCO_ARCH_X86_64,
    .arch_name = "x86-64",
    .has_fpu = 1,
    #if defined(COCO_PLATFORM_WINDOWS)
    .abi = COCO_ABI_MICROSOFT_X64,
    .abi_name = "Microsoft x64",
    .ctx_size = 256,      /* 包含填充 + XMM 寄存器 */
    .ctx_asm_size = 240,  /* 汇编保存到 XMM15 */
    #else
    .abi = COCO_ABI_SYSTEM_V_AMD64,
    .abi_name = "System V AMD64",
    .ctx_size = 72,
    .ctx_asm_size = 56,
    #endif
#elif defined(COCO_ARCH_ARM64)
    .arch = COCO_ARCH_ARM64,
    .arch_name = "ARM64",
    .abi = COCO_ABI_AAPCS64,
    .abi_name = "AAPCS64",
    .has_fpu = 1,
    .ctx_size = 184,      /* 包含 d8-d15 浮点寄存器 */
    .ctx_asm_size = 168,  /* 汇编保存到 d15 */
#else
    .arch = COCO_ARCH_UNKNOWN,
    .arch_name = "Unknown",
    .abi = COCO_ABI_UNKNOWN,
    .abi_name = "Unknown",
    .has_fpu = 0,
    .ctx_size = 0,
    .ctx_asm_size = 0,
#endif
};

const coco_abi_info_t* coco_get_abi_info(void) {
    return &g_abi_info;
}

const char* coco_get_abi_name(void) {
    return g_abi_info.abi_name;
}

/* macOS 特有：运行时架构检测 */
#if defined(COCO_PLATFORM_MACOS)

#include <sys/sysctl.h>

int coco_runtime_is_arm64(void) {
    int value = 0;
    size_t size = sizeof(value);
    /* hw.optional.arm64 在 ARM64 设备上返回 1 */
    if (sysctlbyname("hw.optional.arm64", &value, &size, NULL, 0) == 0) {
        return value;
    }
    /* 如果 sysctl 失败，使用编译时检测 */
#if defined(COCO_ARCH_ARM64)
    return 1;
#else
    return 0;
#endif
}

int coco_runtime_is_rosetta(void) {
    int value = 0;
    size_t size = sizeof(value);
    /* sysctl.proc_translated 在 Rosetta 下返回 1 */
    if (sysctlbyname("sysctl.proc_translated", &value, &size, NULL, 0) == 0) {
        return value;
    }
    return 0;
}

#endif /* COCO_PLATFORM_MACOS */