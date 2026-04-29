/**
 * coco_thread.c - 线程原语抽象实现 (Phase 5)
 *
 * 跨平台线程 API，支持 Linux/macOS/Windows
 */

#include "coco_thread.h"
#include <stdlib.h>
#include <string.h>

#if COCO_PLATFORM_WINDOWS
/* Windows 实现 */

int coco_thread_create(coco_thread_t *thread,
                       const coco_thread_attr_t *attr,
                       coco_thread_func_t func,
                       void *arg) {
    DWORD thread_id;
    HANDLE handle = CreateThread(
        NULL,                           /* 默认安全属性 */
        attr ? attr->stack_size : 0,    /* 栈大小 */
        (LPTHREAD_START_ROUTINE)func,   /* 线程函数 */
        arg,                            /* 参数 */
        0,                              /* 默认创建标志 */
        &thread_id                      /* 线程 ID */
    );

    if (handle == NULL) {
        return -1;
    }

    *thread = handle;
    return 0;
}

int coco_thread_join(coco_thread_t thread, void **retval) {
    WaitForSingleObject(thread, INFINITE);
    if (retval) {
        *retval = NULL;  /* Windows 不支持返回值 */
    }
    CloseHandle(thread);
    return 0;
}

int coco_thread_detach(coco_thread_t thread) {
    CloseHandle(thread);
    return 0;
}

coco_thread_id_t coco_thread_self(void) {
    return GetCurrentThreadId();
}

bool coco_thread_equal(coco_thread_id_t t1, coco_thread_id_t t2) {
    return t1 == t2;
}

void coco_thread_yield(void) {
    SwitchToThread();
}

void coco_thread_sleep(uint32_t ms) {
    Sleep(ms);
}

uint32_t coco_thread_cpu_count(void) {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
}

#else
/* POSIX 实现 (Linux/macOS) */

#include <unistd.h>
#include <sched.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#endif

int coco_thread_create(coco_thread_t *thread,
                       const coco_thread_attr_t *attr,
                       coco_thread_func_t func,
                       void *arg) {
    pthread_attr_t pthread_attr;
    int ret;

    ret = pthread_attr_init(&pthread_attr);
    if (ret != 0) {
        return ret;
    }

    /* 设置栈大小 */
    if (attr && attr->stack_size > 0) {
        ret = pthread_attr_setstacksize(&pthread_attr, attr->stack_size);
        if (ret != 0) {
            pthread_attr_destroy(&pthread_attr);
            return ret;
        }
    }

    ret = pthread_create(thread, &pthread_attr, func, arg);
    pthread_attr_destroy(&pthread_attr);

    return ret;
}

int coco_thread_join(coco_thread_t thread, void **retval) {
    return pthread_join(thread, retval);
}

int coco_thread_detach(coco_thread_t thread) {
    return pthread_detach(thread);
}

coco_thread_id_t coco_thread_self(void) {
    return pthread_self();
}

bool coco_thread_equal(coco_thread_id_t t1, coco_thread_id_t t2) {
    return pthread_equal(t1, t2) != 0;
}

void coco_thread_yield(void) {
    sched_yield();
}

void coco_thread_sleep(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

uint32_t coco_thread_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count <= 0) {
        return 1;
    }
    return (uint32_t)count;
}

#endif /* COCO_PLATFORM_WINDOWS */