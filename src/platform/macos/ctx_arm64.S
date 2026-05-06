/**
 * ctx_arm64.S - macOS ARM64 上下文切换汇编 (Apple Silicon)
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
 *
 * 注意: macOS 符号名需要下划线前缀
 */

.global _coco_ctx_save
.global _coco_ctx_load
.global _coco_ctx_switch

/**
 * _coco_ctx_save - 保存当前上下文
 * 输入: x0 = coco_ctx_t* (目标上下文)
 * 输出: x0 = 0 (首次保存)
 */
_coco_ctx_save:
    // 保存整数 callee-saved 寄存器
    stp     x19, x20, [x0, #24]
    stp     x21, x22, [x0, #40]
    stp     x23, x24, [x0, #56]
    stp     x25, x26, [x0, #72]
    stp     x27, x28, [x0, #88]
    stp     fp, lr, [x0, #8]
    mov     x1, sp
    str     x1, [x0, #0]

    // 保存浮点 callee-saved 寄存器 d8-d15
    stp     d8, d9, [x0, #104]
    stp     d10, d11, [x0, #120]
    stp     d12, d13, [x0, #136]
    stp     d14, d15, [x0, #152]

    mov     x0, #0
    ret

/**
 * _coco_ctx_load - 切换到目标上下文
 * 输入: x0 = coco_ctx_t* (目标上下文)
 * 输出: x0 = 1 (切换成功)
 */
_coco_ctx_load:
    // 加载整数 callee-saved 寄存器
    ldp     x19, x20, [x0, #24]
    ldp     x21, x22, [x0, #40]
    ldp     x23, x24, [x0, #56]
    ldp     x25, x26, [x0, #72]
    ldp     x27, x28, [x0, #88]
    ldp     fp, lr, [x0, #8]
    ldr     x1, [x0, #0]
    mov     sp, x1

    // 加载浮点 callee-saved 寄存器 d8-d15
    ldp     d8, d9, [x0, #104]
    ldp     d10, d11, [x0, #120]
    ldp     d12, d13, [x0, #136]
    ldp     d14, d15, [x0, #152]

    mov     x0, #1
    ret

/**
 * _coco_ctx_switch - 同时保存当前并切换到目标
 * 输入: x0 = current_ctx*, x1 = target_ctx*
 */
_coco_ctx_switch:
    // 保存当前上下文整数寄存器
    stp     x19, x20, [x0, #24]
    stp     x21, x22, [x0, #40]
    stp     x23, x24, [x0, #56]
    stp     x25, x26, [x0, #72]
    stp     x27, x28, [x0, #88]
    stp     fp, lr, [x0, #8]
    mov     x2, sp
    str     x2, [x0, #0]

    // 保存当前上下文浮点寄存器
    stp     d8, d9, [x0, #104]
    stp     d10, d11, [x0, #120]
    stp     d12, d13, [x0, #136]
    stp     d14, d15, [x0, #152]

    // 加载目标上下文整数寄存器
    ldp     x19, x20, [x1, #24]
    ldp     x21, x22, [x1, #40]
    ldp     x23, x24, [x1, #56]
    ldp     x25, x26, [x1, #72]
    ldp     x27, x28, [x1, #88]
    ldp     fp, lr, [x1, #8]
    ldr     x2, [x1, #0]
    mov     sp, x2

    // 加载目标上下文浮点寄存器
    ldp     d8, d9, [x1, #104]
    ldp     d10, d11, [x1, #120]
    ldp     d12, d13, [x1, #136]
    ldp     d14, d15, [x1, #152]

    ret