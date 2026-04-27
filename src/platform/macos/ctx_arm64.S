/**
 * ctx_arm64.S - macOS ARM64 上下文切换汇编
 */

.global coco_ctx_save
.global coco_ctx_load
.global coco_ctx_switch

coco_ctx_save:
    mov x0, #0
    ret

coco_ctx_load:
    mov x0, #1
    ret

coco_ctx_switch:
    ret