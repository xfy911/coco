/**
 * sched.h - 工作窃取调度器 (Phase 1, US-006)
 *
 * 调度 API 声明。
 */

#ifndef SCHED_H
#define SCHED_H

#include "global_sched.h"

/* 查找可运行协程 */
coco_coro_t *find_runnable(coco_processor_t *p);

/* 调度循环 */
coco_coro_t *schedule_once(coco_processor_t *p);
void schedule_done(coco_processor_t *p, coco_coro_t *g);
void schedule_yield(coco_processor_t *p, coco_coro_t *g);
void schedule_block(coco_processor_t *p, coco_coro_t *g);
void schedule_ready(coco_coro_t *g);

/* 负载均衡 */
bool schedule_balanced(coco_global_sched_t *sched);

/* 偷取统计 */
void record_steal_attempt(void);
void record_steal_success(void);
double get_steal_success_rate(void);

#endif /* SCHED_H */
