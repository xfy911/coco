/**
 * coco_cond.h - 条件变量抽象 (Phase 5)
 *
 * 跨平台条件变量 API，支持 Linux/macOS/Windows
 */

#ifndef COCO_COND_H
#define COCO_COND_H

#include "coco_mutex.h"

/* 条件变量类型 */
#if COCO_PLATFORM_WINDOWS
    typedef CONDITION_VARIABLE coco_cond_t;
#else
    typedef pthread_cond_t coco_cond_t;
#endif

/* 条件变量属性 (暂无) */
typedef struct coco_cond_attr {
    int reserved;  /* 保留 */
} coco_cond_attr_t;

/* API */

/**
 * 初始化条件变量
 * @param cond 条件变量
 * @param attr 属性 (NULL = 默认)
 * @return 0 成功，非 0 失败
 */
int coco_cond_init(coco_cond_t *cond, const coco_cond_attr_t *attr);

/**
 * 销毁条件变量
 * @param cond 条件变量
 * @return 0 成功，非 0 失败
 */
int coco_cond_destroy(coco_cond_t *cond);

/**
 * 等待条件变量
 * @param cond 条件变量
 * @param mutex 互斥锁
 * @return 0 成功，非 0 失败
 */
int coco_cond_wait(coco_cond_t *cond, coco_mutex_t *mutex);

/**
 * 超时等待条件变量
 * @param cond 条件变量
 * @param mutex 互斥锁
 * @param ms 超时时间 (毫秒)
 * @return 0 成功，-1 超时，其他失败
 */
int coco_cond_timedwait(coco_cond_t *cond, coco_mutex_t *mutex, uint32_t ms);

/**
 * 通知一个等待线程
 * @param cond 条件变量
 * @return 0 成功，非 0 失败
 */
int coco_cond_signal(coco_cond_t *cond);

/**
 * 通知所有等待线程
 * @param cond 条件变量
 * @return 0 成功，非 0 失败
 */
int coco_cond_broadcast(coco_cond_t *cond);

#endif /* COCO_COND_H */