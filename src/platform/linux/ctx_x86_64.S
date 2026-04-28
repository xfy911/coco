/**
 * ctx_x86_64.S - Linux x86-64 上下文切换汇编 (System V ABI)
 *
 * coco_ctx_t 结构布局:
 *   offset 0:  sp   (栈指针)
 *   offset 8:  rbp  (基址指针)
 *   offset 16: rbx  (callee-saved)
 *   offset 24: r12
 *   offset 32: r13
 *   offset 40: r14
 *   offset 48: r15
 *   offset 56: (padding for alignment)
 *
 * 寄存器保存策略 (System V ABI):
 *   Callee-saved: rbx, rbp, r12-r15
 *   Caller-saved: rax, rcx, rdx, rsi, rdi, r8-r11 (不需要保存)
 */

.global coco_ctx_save
.global coco_ctx_load
.global coco_ctx_switch
.global coco_x86_64_trampoline

/**
 * coco_ctx_save - 保存当前上下文
 * 输入: rdi = coco_ctx_t* (目标上下文)
 * 输出: rax = 0 (首次保存)
 */
coco_ctx_save:
    movq %rsp, 0(%rdi)       # 保存 sp
    movq %rbp, 8(%rdi)       # 保存 rbp
    movq %rbx, 16(%rdi)      # 保存 callee-saved
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)
    xorl %eax, %eax          # 返回 0（首次保存）
    ret

/**
 * coco_ctx_load - 切换到目标上下文
 * 输入: rdi = coco_ctx_t* (目标上下文)
 * 输出: rax = 1 (切换成功)
 */
coco_ctx_load:
    movq 0(%rdi), %rsp       # 恢复 sp
    movq 8(%rdi), %rbp       # 恢复 rbp
    movq 16(%rdi), %rbx
    movq 24(%rdi), %r12
    movq 32(%rdi), %r13
    movq 40(%rdi), %r14
    movq 48(%rdi), %r15
    movl $1, %eax            # 返回 1（切换成功）
    ret

/**
 * coco_ctx_switch - 同时保存当前并切换到目标
 * 输入: rdi = current_ctx*, rsi = target_ctx*
 */
coco_ctx_switch:
    # 保存当前上下文到 rdi
    movq %rsp, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rbx, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)

    # 切换到目标上下文 (rsi)
    movq 0(%rsi), %rsp
    movq 8(%rsi), %rbp
    movq 16(%rsi), %rbx
    movq 24(%rsi), %r12
    movq 32(%rsi), %r13
    movq 40(%rsi), %r14
    movq 48(%rsi), %r15

    ret

/**
 * coco_x86_64_trampoline - 协程入口 trampoline
 *
 * 栈状态 (ret 后):
 *   (%rsp)     = arg    (p[1])
 *   8(%rsp)    = entry  (p[2])
 *
 * 执行流程:
 *   1. 从栈中获取 arg 和 entry
 *   2. 清理栈帧
 *   3. 跳转到 entry(arg)
 */
coco_x86_64_trampoline:
    movq (%rsp), %rdi       # arg -> 第一个参数寄存器
    movq 8(%rsp), %rax      # entry 函数地址
    addq $16, %rsp          # 清理栈帧
    jmp *%rax               # 跳转到 entry(rdi=arg)