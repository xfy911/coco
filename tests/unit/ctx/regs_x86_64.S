/* x86-64 ASM helpers for callee-saved register test */

#ifdef __APPLE__
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

.text
.globl SYM(get_rbx)
.globl SYM(get_rbp)
.globl SYM(get_r12)
.globl SYM(get_r13)
.globl SYM(get_r14)
.globl SYM(get_r15)
.globl SYM(set_callee_saved)

SYM(get_rbx):
    mov %rbx, %rax
    ret

SYM(get_rbp):
    mov %rbp, %rax
    ret

SYM(get_r12):
    mov %r12, %rax
    ret

SYM(get_r13):
    mov %r13, %rax
    ret

SYM(get_r14):
    mov %r14, %rax
    ret

SYM(get_r15):
    mov %r15, %rax
    ret

/* void set_callee_saved(uint64_t rbx, rbp, r12, r13, r14, r15) */
SYM(set_callee_saved):
    mov %rdi, %rbx
    mov %rsi, %rbp
    mov %rdx, %r12
    mov %rcx, %r13
    mov %r8, %r14
    mov %r9, %r15
    ret

.section .note.GNU-stack,"",@progbits
