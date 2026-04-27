/**
 * ctx_arm64.S - macOS ARM64 上下文切换汇编 (Apple Silicon)
 */

.global _coco_ctx_save
.global _coco_ctx_load
.global _coco_ctx_switch

_coco_ctx_save:
    stp x19, x20, [x0, #24]
    stp x21, x22, [x0, #40]
    stp x23, x24, [x0, #56]
    stp x25, x26, [x0, #72]
    stp x27, x28, [x0, #88]
    stp fp, lr, [x0, #8]
    mov x1, sp
    str x1, [x0, #0]
    mov x0, #0
    ret

_coco_ctx_load:
    ldp x19, x20, [x0, #24]
    ldp x21, x22, [x0, #40]
    ldp x23, x24, [x0, #56]
    ldp x25, x26, [x0, #72]
    ldp x27, x28, [x0, #88]
    ldp fp, lr, [x0, #8]
    ldr x1, [x0, #0]
    mov sp, x1
    mov x0, #1
    ret

_coco_ctx_switch:
    stp x19, x20, [x0, #24]
    stp x21, x22, [x0, #40]
    stp x23, x24, [x0, #56]
    stp x25, x26, [x0, #72]
    stp x27, x28, [x0, #88]
    stp fp, lr, [x0, #8]
    mov x2, sp
    str x2, [x0, #0]

    ldp x19, x20, [x1, #24]
    ldp x21, x22, [x1, #40]
    ldp x23, x24, [x1, #56]
    ldp x25, x26, [x1, #72]
    ldp x27, x28, [x1, #88]
    ldp fp, lr, [x1, #8]
    ldr x2, [x1, #0]
    mov sp, x2

    ret