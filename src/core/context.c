/**
 * context.c - 上下文切换平台分发
 */

#include "coco_internal.h"
#include <stdint.h>

/**
 * coco_ctx_init - 初始化协程上下文 (ARM64 版本)
 *
 * 栈布局 (从栈顶向下):
 *   +-------------------+  <- stack_top
 *   |   padding (16B)   |  16 字节对齐
 *   +-------------------+
 *   |   lr = entry      |  返回地址
 *   +-------------------+
 *   |   x0 = arg        |  参数
 *   +-------------------+
 *   |   fp = 0          |  基址指针
 *   +-------------------+
 *   |   x19             |
 *   |   x20             |
 *   |   ...             |
 *   |   x28             |  <- sp
 *   +-------------------+
 */
void coco_ctx_init(coco_ctx_t *ctx, void *stack_top, void (*entry)(void*), void *arg) {
    /* 预留空间: 16B padding + lr + x0 + fp + 10 callee-saved */
    uintptr_t sp = (uintptr_t)stack_top;
    sp -= 112;  /* 预留 112 字节 (14 个 64-bit 值) */
    sp &= ~0xF;  /* 16 字节对齐 */

    uint64_t *p = (uint64_t*)sp;

    /* 构造栈帧 (ARM64) */
    p[0] = 0;               /* fp 初始值（栈底标记） */
    p[1] = (uint64_t)arg;   /* 参数 x0 */
    p[2] = (uint64_t)entry; /* 入口函数地址 (返回地址 lr) */

    /* 设置上下文 */
    ctx->sp = (void*)sp;
    ctx->fp = 0;
    ctx->lr = (void*)entry;
    ctx->x19 = 0;
    ctx->x20 = 0;
    ctx->x21 = 0;
    ctx->x22 = 0;
    ctx->x23 = 0;
    ctx->x24 = 0;
    ctx->x25 = 0;
    ctx->x26 = 0;
    ctx->x27 = 0;
    ctx->x28 = 0;
}