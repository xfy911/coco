/**
 * sched_hooks.c - 调度器钩子系统实现 (US-015)
 */

#include "sched_hooks.h"
#include <stdlib.h>
#include <string.h>

/* 钩子节点 */
typedef struct hook_node {
    coco_hook_fn fn;
    void *data;
    struct hook_node *next;
} hook_node_t;

/* 每种类型的钩子链表 */
static hook_node_t *g_hooks[COCO_HOOK_COUNT] = {NULL};
static int g_hook_count = 0;

/* 注册钩子 */
int coco_hook_register(coco_hook_type_t type, coco_hook_fn fn, void *data) {
    if (type < 0 || type >= COCO_HOOK_COUNT || !fn) {
        return -1;
    }

    hook_node_t *node = malloc(sizeof(hook_node_t));
    if (!node) {
        return -1;
    }

    node->fn = fn;
    node->data = data;
    node->next = g_hooks[type];
    g_hooks[type] = node;
    g_hook_count++;

    return 0;
}

/* 注销钩子 */
int coco_hook_unregister(coco_hook_type_t type, coco_hook_fn fn) {
    if (type < 0 || type >= COCO_HOOK_COUNT || !fn) {
        return -1;
    }

    hook_node_t **pp = &g_hooks[type];
    while (*pp) {
        if ((*pp)->fn == fn) {
            hook_node_t *node = *pp;
            *pp = node->next;
            free(node);
            g_hook_count--;
            return 0;
        }
        pp = &(*pp)->next;
    }

    return -1;
}

/* 调用钩子 */
int coco_hook_invoke(coco_hook_type_t type, coco_coro_t *coro) {
    if (type < 0 || type >= COCO_HOOK_COUNT) {
        return 0;
    }

    hook_node_t *node = g_hooks[type];
    while (node) {
        int result = node->fn(coro, node->data);
        if (result != 0) {
            return result;  /* 钩子返回非0，停止调用 */
        }
        node = node->next;
    }

    return 0;
}

/* 清除所有钩子 */
void coco_hook_clear_all(void) {
    for (int i = 0; i < COCO_HOOK_COUNT; i++) {
        hook_node_t *node = g_hooks[i];
        while (node) {
            hook_node_t *next = node->next;
            free(node);
            node = next;
        }
        g_hooks[i] = NULL;
    }
    g_hook_count = 0;
}

/* 检查钩子是否启用 */
bool coco_hook_enabled(void) {
    return g_hook_count > 0;
}
