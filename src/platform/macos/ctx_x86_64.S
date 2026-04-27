/**
 * ctx_x86_64.S - macOS x86-64 上下文切换汇编 (System V ABI)
 */

.global _coco_ctx_save
.global _coco_ctx_load
.global _coco_ctx_switch

_coco_ctx_save:
    mov [rdi + 0],  rsp
    mov [rdi + 8],  rbp
    mov [rdi + 16], rbx
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    xor eax, eax
    ret

_coco_ctx_load:
    mov rsp, [rdi + 0]
    mov rbp, [rdi + 8]
    mov rbx, [rdi + 16]
    mov r12, [rdi + 24]
    mov r13, [rdi + 32]
    mov r14, [rdi + 40]
    mov r15, [rdi + 48]
    mov eax, 1
    ret

_coco_ctx_switch:
    mov [rdi + 0],  rsp
    mov [rdi + 8],  rbp
    mov [rdi + 16], rbx
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15

    mov rsp, [rsi + 0]
    mov rbp, [rsi + 8]
    mov rbx, [rsi + 16]
    mov r12, [rsi + 24]
    mov r13, [rsi + 32]
    mov r14, [rsi + 40]
    mov r15, [rsi + 48]

    ret