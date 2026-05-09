/**
 * runq.h - 本地运行队列 (Phase 1)
 *
 * Per-P 本地队列，支持工作窃取。
 */

#ifndef RUNQ_H
#define RUNQ_H

#include "global_sched.h"

/* 本地队列最大容量 */
#define LOCAL_RUNQ_MAX 256

/* 本地队列入队 */
int runq_put(coco_processor_t *p, coco_coro_t *g);

/* 本地队列出队 */
coco_coro_t *runq_get(coco_processor_t *p);

/* 工作窃取 - 从目标 P 偷取协程 */
coco_coro_t *runq_steal(coco_processor_t *target);

/* 溢出到全局队列 */
int runq_put_global(coco_coro_t *g);

/* 负载均衡: 将本地队列尾部一半推入全局队列 */
int runq_push_overflow(coco_processor_t *p);

/* 查询 */
uint32_t runq_size(coco_processor_t *p);
bool runq_empty(coco_processor_t *p);

#endif /* RUNQ_H */