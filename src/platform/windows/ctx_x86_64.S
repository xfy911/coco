/**
 * ctx_x86_64.S - Windows x86-64 上下文切换汇编 (Microsoft x64 ABI)
 *
 * coco_ctx_t 结构布局:
 *   offset 0:  sp   (栈指针)
 *   offset 8:  rbp  (基址指针)
 *   offset 16: rbx  (callee-saved)
 *   offset 24: r12
 *   offset 32: r13
 *   offset 40: r14
 *   offset 48: r15
 *
 * Windows x64 调用约定:
 *   参数: rcx, rdx, r8, r9, 然后栈
 *   Callee-saved: rbx, rbp, rsi, rdi, r12-r15, xmm6-xmm15
 *   Caller-saved: rax, rcx, rdx, r8-r11, xmm0-xmm5
 *
 * 注意: Windows 汇编使用 Intel 语法 (dst, src)
 */

.global coco_ctx_save
.global coco_ctx_load
.global coco_ctx_switch
.global coco_x86_64_trampoline

/**
 * coco_ctx_save - 保存当前上下文
 * 输入: rcx = coco_ctx_t* (目标上下文)
 * 输出: rax = 0 (首次保存)
 */
coco_ctx_save:
    mov [rcx + 0],  rsp       ; 保存 sp
    mov [rcx + 8],  rbp       ; 保存 rbp
    mov [rcx + 16], rbx       ; 保存 callee-saved
    mov [rcx + 24], r12
    mov [rcx + 32], r13
    mov [rcx + 40], r14
    mov [rcx + 48], r15
    xor rax, rax              ; 返回 0（首次保存）
    ret

/**
 * coco_ctx_load - 切换到目标上下文
 * 输入: rcx = coco_ctx_t* (目标上下文)
 * 输出: rax = 1 (切换成功)
 */
coco_ctx_load:
    mov rsp, [rcx + 0]        ; 恢复 sp
    mov rbp, [rcx + 8]        ; 恢复 rbp
    mov rbx, [rcx + 16]
    mov r12, [rcx + 24]
    mov r13, [rcx + 32]
    mov r14, [rcx + 40]
    mov r15, [rcx + 48]
    mov rax, 1                ; 返回 1（切换成功）
    ret

/**
 * coco_ctx_switch - 同时保存当前并切换到目标
 * 输入: rcx = current_ctx*, rdx = target_ctx*
 */
coco_ctx_switch:
    ; 保存当前上下文到 rcx
    mov [rcx + 0],  rsp
    mov [rcx + 8],  rbp
    mov [rcx + 16], rbx
    mov [rcx + 24], r12
    mov [rcx + 32], r13
    mov [rcx + 40], r14
    mov [rcx + 48], r15

    ; 切换到目标上下文 (rdx)
    mov rsp, [rdx + 0]
    mov rbp, [rdx + 8]
    mov rbx, [rdx + 16]
    mov r12, [rdx + 24]
    mov r13, [rdx + 32]
    mov r14, [rdx + 40]
    mov r15, [rdx + 48]

    ret

/**
 * coco_x86_64_trampoline - 协程入口 trampoline
 *
 * 栈状态 (ret 后):
 *   (rsp)     = arg    (p[1])
 *   8(rsp)    = entry  (p[2])
 *
 * 执行流程:
 *   1. 从栈中获取 arg 和 entry
 *   2. 清理栈帧
 *   3. 跳转到 entry(arg)
 */
coco_x86_64_trampoline:
    mov rcx, [rsp]            ; arg -> 第一个参数寄存器 (Windows: rcx)
    mov rax, [rsp + 8]        ; entry 函数地址
    add rsp, 16               ; 清理栈帧
    jmp rax                   ; 跳转到 entry(rcx=arg)
