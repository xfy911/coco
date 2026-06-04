#include "coco.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

extern uint64_t get_rbx(void);
extern uint64_t get_rbp(void);
extern uint64_t get_r12(void);
extern uint64_t get_r13(void);
extern uint64_t get_r14(void);
extern uint64_t get_r15(void);
extern void set_callee_saved(uint64_t rbx, uint64_t rbp, uint64_t r12,
                             uint64_t r13, uint64_t r14, uint64_t r15);

static uint64_t g_orig_rbx, g_orig_rbp, g_orig_r12, g_orig_r13, g_orig_r14, g_orig_r15;

static void check_regs_coro(void *arg) {
    (void)arg;
    /* 通过内联汇编调用 set_callee_saved，显式声明 clobber，
     * 避免编译器在调用前后使用这些 callee-saved 寄存器。
     * sub $16, %%rsp 确保 call 前栈保持 16 字节对齐。 */
    __asm__ volatile (
        "mov %%rsp, %%r11\n\t"
        "sub $16, %%rsp\n\t"
        "movabs $0xDEADBEEF11111111, %%rdi\n\t"
        "movabs $0xCAFEBABE22222222, %%rsi\n\t"
        "movabs $0x1234567890ABCDEF, %%rdx\n\t"
        "movabs $0xFEDCBA0987654321, %%rcx\n\t"
        "movabs $0xAAAABBBBCCCCDDDD, %%r8\n\t"
        "movabs $0xBBBBCCCCDDDDEEEE, %%r9\n\t"
        "call set_callee_saved\n\t"
        "mov %%r11, %%rsp\n\t"
        :
        :
        : "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r11", "rax",
          "rbx", "rbp", "r12", "r13", "r14", "r15", "memory"
    );
    coco_yield();
    /* 恢复后继续执行，验证 callee-saved 寄存器保持原值 */
    assert(get_rbx() == 0xDEADBEEF11111111ULL);
    assert(get_rbp() == 0xCAFEBABE22222222ULL);
    assert(get_r12() == 0x1234567890ABCDEFULL);
    assert(get_r13() == 0xFEDCBA0987654321ULL);
    assert(get_r14() == 0xAAAABBBBCCCCDDDDULL);
    assert(get_r15() == 0xBBBBCCCCDDDDEEEEULL);
}

__attribute__((noinline))
static void run_test(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_create(sched, check_regs_coro, NULL, 0);
    coco_sched_run(sched);
    coco_sched_destroy(sched);
}

int main(void) {
    /* 保存主线程原始寄存器值 */
    g_orig_rbx = get_rbx();
    g_orig_rbp = get_rbp();
    g_orig_r12 = get_r12();
    g_orig_r13 = get_r13();
    g_orig_r14 = get_r14();
    g_orig_r15 = get_r15();

    run_test();

    /* 验证调度器运行结束后主线程寄存器已被恢复 */
    assert(get_rbx() == g_orig_rbx);
    assert(get_rbp() == g_orig_rbp);
    assert(get_r12() == g_orig_r12);
    assert(get_r13() == g_orig_r13);
    assert(get_r14() == g_orig_r14);
    assert(get_r15() == g_orig_r15);

    printf("test_ctx_x86_64: PASSED\n");
    return 0;
}
