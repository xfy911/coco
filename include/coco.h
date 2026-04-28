/**
 * coco - A production-grade C coroutine library
 *
 * Supports: Linux/macOS/Windows (x86-64/ARM64)
 * Features: Stackful coroutine, cooperative scheduling, channel, async I/O
 */

#ifndef COCO_H
#define COCO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Error Codes === */
#define COCO_OK                 0
#define COCO_ERROR             -1
#define COCO_ERROR_NOMEM       -2
#define COCO_ERROR_STACK_OVERFLOW -3
#define COCO_ERROR_CHANNEL_CLOSED -4
#define COCO_ERROR_INVALID     -5

/* === Coroutine States === */
typedef enum coco_state {
    COCO_STATE_CREATED,     /* 协程已创建，尚未运行 */
    COCO_STATE_RUNNING,     /* 协程正在运行 */
    COCO_STATE_WAITING,     /* 协程等待 I/O 或 channel */
    COCO_STATE_READY,       /* 协程就绪，等待调度 */
    COCO_STATE_DEAD,        /* 协程已结束 */
    COCO_STATE_OVERFLOW,    /* 协程栈溢出（不可恢复） */
} coco_state_t;

/* === Forward Declarations === */
typedef struct coco_coro coco_coro_t;
typedef struct coco_sched coco_sched_t;
typedef struct coco_channel coco_channel_t;
typedef struct coco_timer coco_timer_t;

/* === Error Callback === */
typedef void (*coco_error_cb)(coco_coro_t *coro, int error_code, const char *msg);

/* === Default Configuration === */
#define COCO_DEFAULT_STACK_SIZE   (64 * 1024)   /* 64KB - 默认值，待遥测验证 */
#define COCO_STACK_SMALL          (16 * 1024)   /* 16KB - I/O 密集，需谨慎 */
#define COCO_STACK_MEDIUM         (32 * 1024)   /* 32KB - 通用 */
#define COCO_STACK_LARGE          (128 * 1024)  /* 128KB - 递归/大栈帧 */
#define COCO_MAX_COROUTINES       10000

/* === Scheduler API === */

/**
 * 创建调度器
 * @return 调度器指针，失败返回 NULL
 */
coco_sched_t *coco_sched_create(void);

/**
 * 销毁调度器
 * @param sched 调度器指针
 */
void coco_sched_destroy(coco_sched_t *sched);

/**
 * 运行调度器直到无协程
 * @param sched 调度器指针
 * @return 0 成功，负数错误码
 */
int coco_sched_run(coco_sched_t *sched);

/**
 * 单次调度（处理一个协程）
 * @param sched 调度器指针
 * @return 0 成功，负数错误码
 */
int coco_sched_run_once(coco_sched_t *sched);

/* === Coroutine Lifecycle API === */

/**
 * 创建协程
 * @param sched 调度器指针
 * @param entry 入口函数
 * @param arg 入口参数
 * @param stack_size 栈大小（0 使用默认值）
 * @return 协程指针，失败返回 NULL
 */
coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size);

/**
 * 退出协程（在协程内部调用）
 * @param coro 协程指针（通常使用 coco_self()）
 * @param result 返回值
 */
void coco_exit(coco_coro_t *coro, void *result);

/**
 * 让出执行权（在协程内部调用）
 */
void coco_yield(void);

/**
 * 等待协程结束并获取结果
 * @param coro 协程指针
 * @return 协程返回值
 */
void *coco_join(coco_coro_t *coro);

/**
 * 销毁协程
 * @param coro 协程指针
 */
void coco_destroy(coco_coro_t *coro);

/* === Coroutine Query API === */

/**
 * 获取当前协程
 * @return 当前协程指针，在调度器上下文返回 NULL
 */
coco_coro_t *coco_self(void);

/**
 * 获取协程状态
 * @param coro 协程指针
 * @return 协程状态
 */
coco_state_t coco_get_state(coco_coro_t *coro);

/**
 * 获取协程 ID
 * @param coro 协程指针
 * @return 协程 ID
 */
uint64_t coco_get_id(coco_coro_t *coro);

/**
 * 设置错误回调
 * @param coro 协程指针
 * @param cb 错误回调函数
 */
void coco_set_error_cb(coco_coro_t *coro, coco_error_cb cb);

/**
 * 获取协程栈使用量
 * @param coro 协程指针
 * @return 已使用字节数，失败返回 0
 *
 * 注意：只采样 yield/exit 点的栈使用，可能低估深度递归峰值
 */
size_t coco_get_stack_usage(coco_coro_t *coro);

/* === Channel API === */

/**
 * 创建 channel
 * @param capacity 缓冲区大小（0 = 无缓冲）
 * @return channel 指针，失败返回 NULL
 */
coco_channel_t *coco_channel_create(size_t capacity);

/**
 * 发送数据（阻塞）
 * @param ch channel 指针
 * @param value 数据指针
 * @return COCO_OK 成功，负数错误码
 */
int coco_channel_send(coco_channel_t *ch, void *value);

/**
 * 接收数据（阻塞）
 * @param ch channel 指针
 * @param value 接收数据指针的指针
 * @return COCO_OK 成功，负数错误码
 */
int coco_channel_recv(coco_channel_t *ch, void **value);

/**
 * 关闭 channel
 * @param ch channel 指针
 */
void coco_channel_close(coco_channel_t *ch);

/**
 * 销毁 channel
 * @param ch channel 指针
 */
void coco_channel_destroy(coco_channel_t *ch);

/* === I/O API === */

/**
 * 协程读取（阻塞）
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 * @return 实际读取字节数，负数错误码
 */
int coco_read(int fd, void *buf, size_t count);

/**
 * 协程写入（阻塞）
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 写入字节数
 * @return 实际写入字节数，负数错误码
 */
int coco_write(int fd, const void *buf, size_t count);

/**
 * 协程 accept（阻塞）
 * @param fd 监听 socket
 * @param addr 客户端地址
 * @param addrlen 地址长度
 * @return 新 socket，负数错误码
 */
int coco_accept(int fd, void *addr, size_t *addrlen);

/**
 * 协程 connect（阻塞）
 * @param fd socket
 * @param addr 目标地址
 * @param addrlen 地址长度
 * @return 0 成功，负数错误码
 */
int coco_connect(int fd, const void *addr, size_t addrlen);

/**
 * 协程 sleep
 * @param ms 毫秒数
 * @return COCO_OK
 */
int coco_sleep(uint64_t ms);

/* === Timer API === */

/**
 * 创建定时器
 * @param delay_ms 延迟毫秒数
 * @param callback 回调函数
 * @param arg 回调参数
 * @return 定时器指针，失败返回 NULL
 */
coco_timer_t *coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg);

/**
 * 取消定时器
 * @param timer 定时器指针
 */
void coco_timer_cancel(coco_timer_t *timer);

#ifdef __cplusplus
}
#endif

#endif /* COCO_H */