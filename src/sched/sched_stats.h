/**
 * sched_stats.h - 调度器统计 API (US-016)
 *
 * 提供单线程和多线程调度器的统计信息
 */

#ifndef COCO_SCHED_STATS_H
#define COCO_SCHED_STATS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单线程调度器统计 */
typedef struct coco_sched_stats {
    uint64_t coroutines_created;    /* 创建的协程总数 */
    uint64_t coroutines_finished;   /* 完成的协程总数 */
    uint64_t context_switches;      /* 上下文切换次数 */
    uint64_t queue_size;            /* 当前队列长度 */
} coco_sched_stats_t;

/* 多线程调度器统计 */
typedef struct coco_global_sched_stats {
    uint64_t coroutines_created;    /* 创建的协程总数 */
    uint64_t coroutines_finished;   /* 完成的协程总数 */
    uint64_t context_switches;      /* 上下文切换次数 */
    uint64_t steals_attempted;      /* 尝试窃取次数 */
    uint64_t steals_succeeded;      /* 成功窃取次数 */
    uint64_t global_queue_size;     /* 全局队列长度 */
    uint32_t p_count;               /* P 的数量 */
    /* 柔性数组：各 P 的本地队列长度 */
    uint32_t local_queue_sizes[];   /* local_queue_sizes[p_count] */
} coco_global_sched_stats_t;

/* 栈池统计 */
typedef struct coco_stack_stats {
    uint64_t total_allocs;          /* 总分配次数 */
    uint64_t pool_hits;             /* 栈池命中次数 */
    uint64_t pool_misses;           /* 栈池未命中次数 */
    uint64_t memory_used;           /* 当前使用的内存（字节） */
} coco_stack_stats_t;

/* 协程统计 */
typedef struct coco_coro_stats {
    uint64_t total_created;         /* 总创建数 */
    uint64_t current_alive;         /* 当前存活数 */
    uint64_t avg_lifetime_ns;       /* 平均生命周期（纳秒） */
} coco_coro_stats_t;

/**
 * 获取单线程调度器统计
 * @param stats 统计结构指针
 * @return 0 成功，-1 失败（调度器未初始化）
 */
int coco_sched_get_stats(coco_sched_stats_t *stats);

/**
 * 获取多线程调度器统计
 * @param stats 统计结构指针（需要分配足够空间）
 * @return 0 成功，-1 失败（调度器未初始化）
 */
int coco_global_sched_get_stats(coco_global_sched_stats_t *stats);

/**
 * 获取栈池统计
 * @param stats 统计结构指针
 * @return 0 成功
 */
int coco_get_stack_stats(coco_stack_stats_t *stats);

/**
 * 获取协程统计
 * @param stats 统计结构指针
 * @return 0 成功
 */
int coco_get_coro_stats(coco_coro_stats_t *stats);

/**
 * 获取多线程调度器统计所需的大小
 * @return 字节数
 */
size_t coco_global_sched_stats_size(void);

#ifdef __cplusplus
}
#endif

#endif /* COCO_SCHED_STATS_H */
