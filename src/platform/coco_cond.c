/**
 * coco_cond.c - 条件变量抽象实现 (Phase 5)
 *
 * 跨平台条件变量 API，支持 Linux/macOS/Windows
 */

#include "coco_cond.h"

#if COCO_PLATFORM_WINDOWS
/* Windows 实现 */

int coco_cond_init(coco_cond_t *cond, const coco_cond_attr_t *attr) {
    (void)attr;
    InitializeConditionVariable(cond);
    return 0;
}

int coco_cond_destroy(coco_cond_t *cond) {
    (void)cond;  /* Windows CONDITION_VARIABLE 不需要销毁 */
    return 0;
}

int coco_cond_wait(coco_cond_t *cond, coco_mutex_t *mutex) {
    return SleepConditionVariableCS(cond, mutex, INFINITE) ? 0 : -1;
}

int coco_cond_timedwait(coco_cond_t *cond, coco_mutex_t *mutex, uint32_t ms) {
    BOOL result = SleepConditionVariableCS(cond, mutex, ms);
    if (result) {
        return 0;
    }
    if (GetLastError() == ERROR_TIMEOUT) {
        return -1;  /* 超时 */
    }
    return -2;  /* 其他错误 */
}

int coco_cond_signal(coco_cond_t *cond) {
    WakeConditionVariable(cond);
    return 0;
}

int coco_cond_broadcast(coco_cond_t *cond) {
    WakeAllConditionVariable(cond);
    return 0;
}

#else
/* POSIX 实现 (Linux/macOS) */

#include <time.h>
#include <errno.h>

int coco_cond_init(coco_cond_t *cond, const coco_cond_attr_t *attr) {
    (void)attr;
    return pthread_cond_init(cond, NULL);
}

int coco_cond_destroy(coco_cond_t *cond) {
    return pthread_cond_destroy(cond);
}

int coco_cond_wait(coco_cond_t *cond, coco_mutex_t *mutex) {
    return pthread_cond_wait(cond, mutex);
}

int coco_cond_timedwait(coco_cond_t *cond, coco_mutex_t *mutex, uint32_t ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    int ret = pthread_cond_timedwait(cond, mutex, &ts);
    if (ret == ETIMEDOUT) {
        return -1;  /* 超时 */
    }
    return ret;
}

int coco_cond_signal(coco_cond_t *cond) {
    return pthread_cond_signal(cond);
}

int coco_cond_broadcast(coco_cond_t *cond) {
    return pthread_cond_broadcast(cond);
}

#endif /* COCO_PLATFORM_WINDOWS */