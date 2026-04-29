/**
 * coco_mutex.h - 互斥锁抽象 (Phase 5)
 *
 * 跨平台互斥锁 API，支持 Linux/macOS/Windows
 */

#ifndef COCO_MUTEX_H
#define COCO_MUTEX_H

#include "coco_thread.h"

/* 互斥锁类型 */
#if COCO_PLATFORM_WINDOWS
    typedef CRITICAL_SECTION coco_mutex_t;
#else
    typedef pthread_mutex_t coco_mutex_t;
#endif

/* 互斥锁属性 */
typedef struct coco_mutex_attr {
    int type;  /* 0 = 普通, 1 = 递归 */
} coco_mutex_attr_t;

/* API */

/**
 * 初始化互斥锁
 * @param mutex 互斥锁
 * @param attr 属性 (NULL = 默认)
 * @return 0 成功，非 0 失败
 */
int coco_mutex_init(coco_mutex_t *mutex, const coco_mutex_attr_t *attr);

/**
 * 销毁互斥锁
 * @param mutex 互斥锁
 * @return 0 成功，非 0 失败
 */
int coco_mutex_destroy(coco_mutex_t *mutex);

/**
 * 加锁
 * @param mutex 互斥锁
 * @return 0 成功，非 0 失败
 */
int coco_mutex_lock(coco_mutex_t *mutex);

/**
 * 尝试加锁
 * @param mutex 互斥锁
 * @return 0 成功，非 0 失败
 */
int coco_mutex_trylock(coco_mutex_t *mutex);

/**
 * 解锁
 * @param mutex 互斥锁
 * @return 0 成功，非 0 失败
 */
int coco_mutex_unlock(coco_mutex_t *mutex);

#endif /* COCO_MUTEX_H */