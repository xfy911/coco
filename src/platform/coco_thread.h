/**
 * coco_thread.h - 线程原语抽象 (Phase 5)
 *
 * 跨平台线程 API，支持 Linux/macOS/Windows
 */

#ifndef COCO_THREAD_H
#define COCO_THREAD_H

#include <stdint.h>
#include <stdbool.h>

/* 平台检测 */
#if defined(_WIN32) || defined(_WIN64)
    #define COCO_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #define COCO_PLATFORM_MACOS 1
#elif defined(__linux__)
    #define COCO_PLATFORM_LINUX 1
#else
    #error "Unsupported platform"
#endif

/* 线程句柄类型 */
#if COCO_PLATFORM_WINDOWS
    #include <windows.h>
    typedef HANDLE coco_thread_t;
    typedef DWORD coco_thread_id_t;
    #define COCO_THREAD_INVALID NULL
#else
    #include <pthread.h>
    typedef pthread_t coco_thread_t;
    typedef pthread_t coco_thread_id_t;
    #define COCO_THREAD_INVALID 0
#endif

/* 线程函数类型 */
typedef void* (*coco_thread_func_t)(void* arg);

/* 线程属性 */
typedef struct coco_thread_attr {
    size_t stack_size;      /* 栈大小 (0 = 默认) */
    int priority;           /* 优先级 (0 = 默认) */
    const char *name;       /* 线程名 (调试用) */
} coco_thread_attr_t;

/* API */

/**
 * 创建线程
 * @param thread 线程句柄
 * @param attr 线程属性 (NULL = 默认)
 * @param func 线程函数
 * @param arg 线程参数
 * @return 0 成功，非 0 失败
 */
int coco_thread_create(coco_thread_t *thread,
                       const coco_thread_attr_t *attr,
                       coco_thread_func_t func,
                       void *arg);

/**
 * 等待线程结束
 * @param thread 线程句柄
 * @param retval 返回值指针 (可选)
 * @return 0 成功，非 0 失败
 */
int coco_thread_join(coco_thread_t thread, void **retval);

/**
 * 分离线程
 * @param thread 线程句柄
 * @return 0 成功，非 0 失败
 */
int coco_thread_detach(coco_thread_t thread);

/**
 * 获取当前线程 ID
 * @return 线程 ID
 */
coco_thread_id_t coco_thread_self(void);

/**
 * 比较两个线程 ID
 * @param t1 线程 ID 1
 * @param t2 线程 ID 2
 * @return true 相等，false 不等
 */
bool coco_thread_equal(coco_thread_id_t t1, coco_thread_id_t t2);

/**
 * 线程让出
 */
void coco_thread_yield(void);

/**
 * 线程休眠
 * @param ms 毫秒
 */
void coco_thread_sleep(uint32_t ms);

/**
 * 获取 CPU 核心数
 * @return CPU 核心数
 */
uint32_t coco_thread_cpu_count(void);

#endif /* COCO_THREAD_H */