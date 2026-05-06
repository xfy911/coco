/**
 * ctx_x86_64.S - Windows x86-64 上下文切换汇编 (Microsoft x64 ABI)
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
    mov     [rcx + 0],  rsp       ; 保存 sp
    mov     [rcx + 8],  rbp       ; 保存 rbp
    mov     [rcx + 16], rbx       ; 保存 callee-saved
    mov     [rcx + 24], rsi       ; 保存 rsi (Windows callee-saved)
    mov     [rcx + 32], rdi       ; 保存 rdi (Windows callee-saved)
    mov     [rcx + 40], r12
    mov     [rcx + 48], r13
    mov     [rcx + 56], r14
    mov     [rcx + 64], r15
    /* XMM 偏移从 80 开始，16 字节对齐 */
    movdqa  [rcx + 80],  xmm6
    movdqa  [rcx + 96],  xmm7
    movdqa  [rcx + 112], xmm8
    movdqa  [rcx + 128], xmm9
    movdqa  [rcx + 144], xmm10
    movdqa  [rcx + 160], xmm11
    movdqa  [rcx + 176], xmm12
    movdqa  [rcx + 192], xmm13
    movdqa  [rcx + 208], xmm14
    movdqa  [rcx + 224], xmm15
    xor     rax, rax              ; 返回 0（首次保存）
    ret

/**
 * coco_ctx_load - 切换到目标上下文
 * 输入: rcx = coco_ctx_t* (目标上下文)
 * 输出: rax = 1 (切换成功)
 */
coco_ctx_load:
    mov     rsp, [rcx + 0]        ; 恢复 sp
    mov     rbp, [rcx + 8]        ; 恢复 rbp
    mov     rbx, [rcx + 16]
    mov     rsi, [rcx + 24]       ; 恢复 rsi
    mov     rdi, [rcx + 32]       ; 恢复 rdi
    mov     r12, [rcx + 40]
    mov     r13, [rcx + 48]
    mov     r14, [rcx + 56]
    mov     r15, [rcx + 64]
    movdqa  xmm6,  [rcx + 80]
    movdqa  xmm7,  [rcx + 96]
    movdqa  xmm8,  [rcx + 112]
    movdqa  xmm9,  [rcx + 128]
    movdqa  xmm10, [rcx + 144]
    movdqa  xmm11, [rcx + 160]
    movdqa  xmm12, [rcx + 176]
    movdqa  xmm13, [rcx + 192]
    movdqa  xmm14, [rcx + 208]
    movdqa  xmm15, [rcx + 224]
    mov     rax, 1                ; 返回 1（切换成功）
    ret

/**
 * coco_ctx_switch - 同时保存当前并切换到目标
 * 输入: rcx = current_ctx*, rdx = target_ctx*
 */
coco_ctx_switch:
    ; 保存当前上下文到 rcx
    mov     [rcx + 0],  rsp
    mov     [rcx + 8],  rbp
    mov     [rcx + 16], rbx
    mov     [rcx + 24], rsi
    mov     [rcx + 32], rdi
    mov     [rcx + 40], r12
    mov     [rcx + 48], r13
    mov     [rcx + 56], r14
    mov     [rcx + 64], r15
    movdqa  [rcx + 80],  xmm6
    movdqa  [rcx + 96],  xmm7
    movdqa  [rcx + 112], xmm8
    movdqa  [rcx + 128], xmm9
    movdqa  [rcx + 144], xmm10
    movdqa  [rcx + 160], xmm11
    movdqa  [rcx + 176], xmm12
    movdqa  [rcx + 192], xmm13
    movdqa  [rcx + 208], xmm14
    movdqa  [rcx + 224], xmm15

    ; 切换到目标上下文 (rdx)
    mov     rsp, [rdx + 0]
    mov     rbp, [rdx + 8]
    mov     rbx, [rdx + 16]
    mov     rsi, [rdx + 24]
    mov     rdi, [rdx + 32]
    mov     r12, [rdx + 40]
    mov     r13, [rdx + 48]
    mov     r14, [rdx + 56]
    mov     r15, [rdx + 64]
    movdqa  xmm6,  [rdx + 80]
    movdqa  xmm7,  [rdx + 96]
    movdqa  xmm8,  [rdx + 112]
    movdqa  xmm9,  [rdx + 128]
    movdqa  xmm10, [rdx + 144]
    movdqa  xmm11, [rdx + 160]
    movdqa  xmm12, [rdx + 176]
    movdqa  xmm13, [rdx + 192]
    movdqa  xmm14, [rdx + 208]
    movdqa  xmm15, [rdx + 224]

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
    mov     rcx, [rsp]            ; arg -> 第一个参数寄存器 (Windows: rcx)
    mov     rax, [rsp + 8]        ; entry 函数地址
    add     rsp, 16               ; 清理栈帧
    jmp     rax                   ; 跳转到 entry(rcx=arg)