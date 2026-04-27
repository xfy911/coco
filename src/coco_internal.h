/**
 * coco_internal.h - 内部头文件
 */

#ifndef COCO_INTERNAL_H
#define COCO_INTERNAL_H

#include "coco.h"
#include <stdint.h>

/* 上下文结构 (与汇编对应，支持 x86-64 和 ARM64) */
typedef struct coco_ctx {
    void *sp;       /* offset 0: 栈指针 */
    void *fp;       /* offset 8: 基址指针 (ARM64: x29, x86-64: rbp) */
    void *lr;       /* offset 16: 链接寄存器 (ARM64: x30) */
    void *x19;      /* offset 24: callee-saved */
    void *x20;      /* offset 32 */
    void *x21;      /* offset 40 */
    void *x22;      /* offset 48 */
    void *x23;      /* offset 56 */
    void *x24;      /* offset 64 */
    void *x25;      /* offset 72 */
    void *x26;      /* offset 80 */
    void *x27;      /* offset 88 */
    void *x28;      /* offset 96 */
    /* x86-64 使用 rbp, rbx, r12-r15 (复用部分字段) */
} coco_ctx_t;

/* 上下文 API (汇编实现) */
void coco_ctx_save(coco_ctx_t *ctx);
void coco_ctx_load(coco_ctx_t *ctx);
void coco_ctx_switch(coco_ctx_t *current, coco_ctx_t *target);

/* 上下文初始化 (C 实现) */
void coco_ctx_init(coco_ctx_t *ctx, void *stack_top, void (*entry)(void*), void *arg);

#endif /* COCO_INTERNAL_H */