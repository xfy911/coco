/**
 * channel_mt.h - Channel 多线程支持 (Phase 1, US-007)
 *
 * 为 channel 添加互斥锁保护，支持多线程场景。
 */

#ifndef CHANNEL_MT_H
#define CHANNEL_MT_H

#include "../coco_internal.h"
#include <pthread.h>
#include <stdbool.h>

/* 多线程 Channel 结构 */
struct coco_channel_mt {
    size_t capacity;          /* 缓冲区大小（0 = 无缓冲） */
    int closed;               /* 是否已关闭 */

    /* 互斥锁保护 */
    pthread_mutex_t lock;

    /* 有缓冲 channel: 环形缓冲区 */
    void **buffer;
    size_t head;              /* 读位置 */
    size_t tail;              /* 写位置 */
    size_t count;             /* 当前元素数 */

    /* 等待队列 */
    coco_coro_t *send_wait_head;
    coco_coro_t *send_wait_tail;
    coco_coro_t *recv_wait_head;
    coco_coro_t *recv_wait_tail;

    /* 条件变量 (用于非协程线程) */
    pthread_cond_t send_cond;
    pthread_cond_t recv_cond;
};

typedef struct coco_channel_mt coco_channel_mt_t;

/* API */
coco_channel_mt_t *coco_channel_mt_create(size_t capacity);
void coco_channel_mt_destroy(coco_channel_mt_t *ch);

/* 协程 API (在协程上下文中调用) */
int coco_channel_mt_send(coco_channel_mt_t *ch, void *value);
int coco_channel_mt_recv(coco_channel_mt_t *ch, void **value);

/* 线程 API (在普通线程中调用，阻塞) */
int coco_channel_mt_send_thread(coco_channel_mt_t *ch, void *value);
int coco_channel_mt_recv_thread(coco_channel_mt_t *ch, void **value);

/* 非阻塞 API */
int coco_channel_mt_try_send(coco_channel_mt_t *ch, void *value);
int coco_channel_mt_try_recv(coco_channel_mt_t *ch, void **value);

/* 关闭 */
void coco_channel_mt_close(coco_channel_mt_t *ch);

/* 查询 */
size_t coco_channel_mt_len(coco_channel_mt_t *ch);
bool coco_channel_mt_is_closed(coco_channel_mt_t *ch);

#endif /* CHANNEL_MT_H */
