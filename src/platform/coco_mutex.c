/**
 * coco_mutex.c - 互斥锁抽象实现 (Phase 5)
 *
 * 跨平台互斥锁 API，支持 Linux/macOS/Windows
 */

#include "coco_mutex.h"

#if COCO_PLATFORM_WINDOWS
/* Windows 实现 */

int coco_mutex_init(coco_mutex_t *mutex, const coco_mutex_attr_t *attr) {
    (void)attr;  /* Windows CRITICAL_SECTION 不支持属性 */
    InitializeCriticalSection(mutex);
    return 0;
}

int coco_mutex_destroy(coco_mutex_t *mutex) {
    DeleteCriticalSection(mutex);
    return 0;
}

int coco_mutex_lock(coco_mutex_t *mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

int coco_mutex_trylock(coco_mutex_t *mutex) {
    return TryEnterCriticalSection(mutex) ? 0 : -1;
}

int coco_mutex_unlock(coco_mutex_t *mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

#else
/* POSIX 实现 (Linux/macOS) */

int coco_mutex_init(coco_mutex_t *mutex, const coco_mutex_attr_t *attr) {
    if (attr && attr->type == 1) {
        /* 递归锁 */
        pthread_mutexattr_t mutex_attr;
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
        int ret = pthread_mutex_init(mutex, &mutex_attr);
        pthread_mutexattr_destroy(&mutex_attr);
        return ret;
    }
    return pthread_mutex_init(mutex, NULL);
}

int coco_mutex_destroy(coco_mutex_t *mutex) {
    return pthread_mutex_destroy(mutex);
}

int coco_mutex_lock(coco_mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

int coco_mutex_trylock(coco_mutex_t *mutex) {
    return pthread_mutex_trylock(mutex);
}

int coco_mutex_unlock(coco_mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

#endif /* COCO_PLATFORM_WINDOWS */