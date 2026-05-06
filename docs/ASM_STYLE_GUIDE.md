# Coco 汇编编码规范

本文档定义了 Coco 协程库所有汇编文件的编码规范，确保跨平台一致性和可维护性。

## 文件头模板

每个汇编文件必须包含以下头部注释：

```asm
/**
 * ctx_{arch}.S - {Platform} {Architecture} 上下文切换汇编
 *
 * 调用约定: {ABI Name}
 * Callee-saved: {寄存器列表}
 *
 * coco_ctx_t 结构布局 ({Architecture}):
 *   offset 0-XX:   {字段描述}
 *   offset XX-YY:  {字段描述}
 *   offset YY-ZZ:  stack_base, stack_limit (C 管理，汇编不保存)
 *
 * 性能目标: < {目标}ns
 *
 * 注意: {平台特定说明}
 */
```

### 示例

**Linux ARM64**:
```asm
/**
 * ctx_arm64.S - Linux ARM64 上下文切换汇编
 *
 * 调用约定: AAPCS64
 * Callee-saved: x19-x28, fp(x29), lr(x30), d8-d15
 *
 * coco_ctx_t 结构布局 (ARM64):
 *   offset 0-96:   sp, fp, lr, x19-x28
 *   offset 104-160: d8-d15
 *   offset 168-176: stack_base, stack_limit (C 管理，汇编不保存)
 *
 * 性能目标: < 100ns
 */
```

**Windows x86-64**:
```asm
/**
 * ctx_x86_64.S - Windows x86-64 上下文切换汇编
 *
 * 调用约定: Microsoft x64 ABI
 * Callee-saved: rbx, rbp, rsi, rdi, r12-r15, xmm6-xmm15
 *
 * coco_ctx_t 结构布局 (Windows x86-64):
 *   offset 0-64:   sp, fp, rbx, rsi, rdi, r12-r15
 *   offset 72:     _pad0 (8字节填充)
 *   offset 80-224: xmm6-xmm15 (每个 16 字节，16 字节对齐!)
 *   offset 240-248: stack_base, stack_limit (C 管理，汇编不保存)
 *
 * 性能目标: < 150ns (含 XMM 保存)
 *
 * 注意: Windows 汇编使用 Intel 语法 (dst, src)
 */
```

## 命名规范

### 函数命名

| 函数 | 描述 |
|------|------|
| `coco_ctx_save` | 保存当前上下文 |
| `coco_ctx_load` | 切换到目标上下文 |
| `coco_ctx_switch` | 同时保存当前并切换到目标 |
| `coco_x86_64_trampoline` | x86-64 协程入口引导 |

### 符号前缀

| 平台 | 前缀 | 示例 |
|------|------|------|
| Linux | 无 | `coco_ctx_save` |
| macOS | `_` | `_coco_ctx_save` |
| Windows | 无 | `coco_ctx_save` |

### 局部标签

使用 `.L{purpose}_{number}` 格式：
```asm
.Lsave_int_1:
.Lsave_fp_1:
.Lload_done:
```

### 宏定义

使用 `COCO_{NAME}` 格式：
```c
#define COCO_CTX_SIZE 184
#define COCO_CTX_ASM_SIZE 168
```

## 注释规范

### 函数注释

每个函数必须有完整的注释块：

```asm
/**
 * coco_ctx_save - 保存当前上下文
 * 输入: x0 = coco_ctx_t* (目标上下文)
 * 输出: x0 = 0 (首次保存)
 */
coco_ctx_save:
```

### 行内注释

每个寄存器操作必须有行内注释：

```asm
stp     x19, x20, [x0, #24]    // 保存 callee-saved
stp     d8, d9, [x0, #104]     // 保存浮点 callee-saved
mov     x0, #0                 // 返回 0（首次保存）
```

### 块注释

复杂逻辑必须有块注释解释：

```asm
// 保存整数 callee-saved 寄存器
// x19-x28 是 AAPCS64 callee-saved
stp     x19, x20, [x0, #24]
...
```

### 性能注释

性能关键路径标注 `// PERF:`：

```asm
// PERF: 使用 stp/ldp 成对操作减少指令数
stp     x19, x20, [x0, #24]
stp     x21, x22, [x0, #40]
```

## 汇编语法

### x86-64

| 平台 | 语法 | 示例 |
|------|------|------|
| Linux | AT&T | `movq %rsp, 0(%rdi)` |
| macOS | Intel | `mov [rdi + 0], rsp` |
| Windows | Intel | `mov [rcx + 0], rsp` |

### ARM64

所有 ARM64 平台使用相同的 ARM 汇编语法：
```asm
stp     x19, x20, [x0, #24]
ldp     d8, d9, [x0, #104]
```

## 寄存器保存规范

### x86-64 (System V ABI)

| 寄存器 | 类型 | 是否保存 |
|--------|------|----------|
| rax | caller-saved | 否 |
| rbx | callee-saved | **是** |
| rbp | callee-saved | **是** |
| r12-r15 | callee-saved | **是** |
| rsp | 栈指针 | **是** |

### x86-64 (Microsoft x64 ABI)

| 寄存器 | 类型 | 是否保存 |
|--------|------|----------|
| rax | caller-saved | 否 |
| rbx | callee-saved | **是** |
| rbp | callee-saved | **是** |
| rsi, rdi | callee-saved | **是** |
| r12-r15 | callee-saved | **是** |
| xmm6-xmm15 | callee-saved | **是** |
| rsp | 栈指针 | **是** |

### ARM64 (AAPCS64)

| 寄存器 | 类型 | 是否保存 |
|--------|------|----------|
| x0-x18 | caller-saved | 否 |
| x19-x28 | callee-saved | **是** |
| fp (x29) | callee-saved | **是** |
| lr (x30) | callee-saved | **是** |
| d0-d7 | caller-saved | 否 |
| d8-d15 | callee-saved | **是** |
| sp | 栈指针 | **是** |

## 性能目标

| 平台 | 架构 | 目标延迟 |
|------|------|----------|
| Linux | x86-64 | < 100ns |
| Linux | ARM64 | < 100ns |
| macOS | x86-64 | < 100ns |
| macOS | ARM64 | < 100ns |
| Windows | x86-64 | < 150ns (含 XMM) |
| Windows | ARM64 | < 100ns |

## 测试要求

每个平台汇编实现必须有对应的测试：

1. **寄存器完整性测试**：验证所有 callee-saved 寄存器在上下文切换后保持不变
2. **浮点寄存器测试**：验证浮点寄存器正确保存/恢复
3. **性能基准测试**：验证上下文切换延迟满足目标

## 修改检查清单

修改汇编文件时，必须检查：

- [ ] 文件头注释完整且准确
- [ ] 所有函数有完整注释
- [ ] 行内注释清晰
- [ ] 寄存器保存列表与 ABI 一致
- [ ] 偏移量与 `coco_ctx_t` 结构体定义一致
- [ ] XMM 寄存器偏移 16 字节对齐（Windows x86-64）
- [ ] 符号名前缀正确（macOS 需要 `_`）
- [ ] 性能基准测试通过
