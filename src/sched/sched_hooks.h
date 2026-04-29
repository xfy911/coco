/**
 * sched_hooks.h - 调度器钩子系统 (US-015)
 *
 * 支持在协程生命周期关键点注册回调
 */

#ifndef COCO_SCHED_HOOKS_H
#define COCO_SCHED_HOOKS_H

#include "../coco_internal.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 钩子类型
 */
typedef enum {
    COCO_HOOK_ON_CREATE,      /* 协程创建，返回非0可拒绝创建 */
    COCO_HOOK_ON_SCHEDULE,    /* 协程调度 */
    COCO_HOOK_ON_EXIT,        /* 协程退出 */
    COCO_HOOK_ON_DESTROY,     /* 协程销毁 */
    COCO_HOOK_ON_STEAL,       /* 工作窃取 */
    COCO_HOOK_ON_BLOCK,       /* 协程阻塞 */
    COCO_HOOK_ON_WAKE,        /* 协程唤醒 */
    COCO_HOOK_COUNT           /* 钩子类型数量 */
} coco_hook_type_t;

/**
 * @brief 钩子函数类型
 * @param coro 协程指针
 * @param data 用户数据
 * @return 0 继续，非0 中断操作（仅对 ON_CREATE 有效）
 */
typedef int (*coco_hook_fn)(coco_coro_t *coro, void *data);

/**
 * @brief 注册钩子
 * @param type 钩子类型
 * @param fn 钩子函数
 * @param data 用户数据
 * @return 0 成功，-1 失败
 */
int coco_hook_register(coco_hook_type_t type, coco_hook_fn fn, void *data);

/**
 * @brief 注销钩子
 * @param type 钩子类型
 * @param fn 钩子函数
 * @return 0 成功，-1 未找到
 */
int coco_hook_unregister(coco_hook_type_t type, coco_hook_fn fn);

/**
 * @brief 调用钩子
 * @param type 钩子类型
 * @param coro 协程指针
 * @return 0 所有钩子返回0，非0 有钩子返回非0
 *
 * 按注册顺序调用所有钩子，任一钩子返回非0则停止。
 */
int coco_hook_invoke(coco_hook_type_t type, coco_coro_t *coro);

/**
 * @brief 清除所有钩子
 */
void coco_hook_clear_all(void);

/**
 * @brief 检查钩子是否启用
 * @return true 如果有任何钩子注册
 */
bool coco_hook_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* COCO_SCHED_HOOKS_H */
