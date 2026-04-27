/**
 * ctx_x86_64.S - macOS x86-64 上下文切换汇编
 */

.global coco_ctx_save
.global coco_ctx_load
.global coco_ctx_switch

coco_ctx_save:
    xor eax, eax
    ret

coco_ctx_load:
    mov eax, 1
    ret

coco_ctx_switch:
    ret