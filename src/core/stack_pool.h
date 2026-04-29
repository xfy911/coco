/**
 * stack_pool.h - 栈池头文件
 *
 * 单线程设计：栈池仅限单调度器使用，无需同步
 */

#ifndef STACK_POOL_H
#define STACK_POOL_H

#include <stddef.h>
#include <stdint.h>

/* 栈池大小类 */
#define STACK_POOL_NUM_CLASSES  4
#define STACK_POOL_CLASS_LIMIT  256  /* 每个 size class 上限 */

/* Size classes: 16KB, 32KB, 64KB, 128KB */
#define STACK_SIZE_16K   (16 * 1024)
#define STACK_SIZE_32K   (32 * 1024)
#define STACK_SIZE_64K   (64 * 1024)
#define STACK_SIZE_128K  (128 * 1024)

/* 栈池选择性清零模式 */
typedef enum {
    STACK_ZERO_NONE = 0,      /* 不清零（最快，需确保协程入口初始化所有状态） */
    STACK_ZERO_TOP_1K = 1,    /* 仅清零栈顶 1KB（推荐，平衡安全与性能） */
    STACK_ZERO_FULL = 2       /* 清零全部（最安全，最慢） */
} stack_zero_mode_t;

/* 空闲栈节点 - 嵌入栈空间 */
typedef struct stack_node {
    void *stack_top;          /* 栈顶地址 */
    size_t size;              /* 栈大小 */
    struct stack_node *next;  /* 链表下一个 */
} stack_node_t;

/* 栈池结构 */
typedef struct stack_pool {
    stack_node_t *freelists[STACK_POOL_NUM_CLASSES];  /* 每个 size class 一个空闲链表 */
    size_t sizes[STACK_POOL_NUM_CLASSES];              /* Size class 大小 */
    uint32_t counts[STACK_POOL_NUM_CLASSES];           /* 每个 class 当前数量 */
    uint32_t limits[STACK_POOL_NUM_CLASSES];           /* 每个 class 上限 */
    stack_zero_mode_t zero_mode;                        /* 清零模式 */
} stack_pool_t;

/* API */
stack_pool_t *stack_pool_create(void);
void stack_pool_destroy(stack_pool_t *pool);
void *stack_pool_alloc(stack_pool_t *pool, size_t size);
void stack_pool_free(stack_pool_t *pool, void *stack_top, size_t size);

#endif /* STACK_POOL_H */
