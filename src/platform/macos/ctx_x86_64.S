/**
 * ctx_x86_64.S - macOS x86-64 上下文切换汇编 (System V ABI)
 */

.global _coco_ctx_save
.global _coco_ctx_load
.global _coco_ctx_switch
.global _coco_x86_64_trampoline

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

/**
 * coco_x86_64_trampoline - 协程入口 trampoline
 *
 * 栈状态 (ret 后):
 *   [rsp]     = arg    (p[1])
 *   [rsp + 8] = entry  (p[2])
 *
 * 执行流程:
 *   1. 从栈中获取 arg 和 entry
 *   2. 清理栈帧
 *   3. 跳转到 entry(arg)
 */
_coco_x86_64_trampoline:
    mov rdi, [rsp]         ; arg -> 第一个参数寄存器
    mov rax, [rsp + 8]     ; entry 函数地址
    add rsp, 16            ; 清理栈帧
    jmp rax                ; 跳转到 entry(rdi=arg)