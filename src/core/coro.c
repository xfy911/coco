/**
 * coro.c - 协程生命周期管理
 */

#include "../coco_internal.h"
#include "stack_pool.h"
#include "hot_stack.h"
#include "../../include/coco_stack_map.h"
#include "../sched/global_sched.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 线程局部存储（多线程模式） */
_Thread_local coco_sched_t *g_current_sched = NULL;
_Thread_local coco_coro_t *g_current_coro = NULL;
_Thread_local coco_ctx_t *g_return_ctx = NULL;

/* 抢占 API 声明 */
int coco_preempt_init(void);
void coco_preempt_cleanup(void);
int coco_preempt_arm(void);
int coco_preempt_disarm(void);

/* 协程入口包装函数 */
void coro_entry_wrapper(void *arg) {
    (void)arg;
    coco_coro_t *coro = coco_self();
    if (coro && coro->entry) {
        coro->entry(coro->arg);
    }
    coco_exit(coro, NULL);
}

/* 获取当前时间戳（毫秒） */
static uint64_t get_timestamp_ms(void) {
    return get_current_time_ms_internal();
}

/* 入队：添加到对应优先级队列尾部 */
void enqueue_ready(coco_sched_t *sched, coco_coro_t *coro) {
    coco_priority_t prio = coro->priority;

    coro->state = COCO_STATE_READY;
    coro->prev = sched->ready_tails[prio];
    coro->next = NULL;
    coro->ready_timestamp = get_timestamp_ms();

    if (sched->ready_tails[prio]) {
        sched->ready_tails[prio]->next = coro;
    } else {
        sched->ready_heads[prio] = coro;
    }
    sched->ready_tails[prio] = coro;
    sched->ready_counts[prio]++;
    sched->ready_count++;

    /* 设置位图对应位 */
    sched->ready_bitmap |= (1U << prio);
}

/* === 调度器 API === */

coco_sched_t *coco_sched_create(void) {
    coco_sched_t *sched = calloc(1, sizeof(coco_sched_t));
    if (!sched) {
        return NULL;
    }

    sched->coro_capacity = COCO_MAX_COROUTINES;
    sched->coro_table = calloc(sched->coro_capacity, sizeof(coco_coro_t*));
    if (!sched->coro_table) {
        free(sched);
        return NULL;
    }

    sched->next_id = 1;
    sched->poll_fd = -1;
    g_current_sched = sched;

    /* 初始化信号处理 */
    if (coco_signal_init(sched) != COCO_OK) {
        free(sched->coro_table);
        free(sched);
        return NULL;
    }

    /* 初始化时间轮 */
    sched->timer_wheel = coco_timer_wheel_create();
    if (!sched->timer_wheel) {
        coco_signal_cleanup();
        free(sched->coro_table);
        free(sched);
        return NULL;
    }

    /* 初始化 I/O 多路复用 */
    if (coco_poll_init(sched) != COCO_OK) {
        coco_timer_wheel_destroy(sched->timer_wheel);
        coco_signal_cleanup();
        free(sched->coro_table);
        free(sched);
        return NULL;
    }

    /* 初始化栈池 */
    sched->stack_pool = stack_pool_create();
    if (!sched->stack_pool) {
        coco_poll_cleanup(sched);
        coco_timer_wheel_destroy(sched->timer_wheel);
        coco_signal_cleanup();
        free(sched->coro_table);
        free(sched);
        return NULL;
    }

    if (coco_hot_slots_init(sched, COCO_HOT_SLOTS_DEFAULT) != COCO_OK) {
        stack_pool_destroy(sched->stack_pool);
        coco_poll_cleanup(sched);
        coco_timer_wheel_destroy(sched->timer_wheel);
        coco_signal_cleanup();
        free(sched->coro_table);
        free(sched);
        return NULL;
    }

    /* 初始化老化阈值：100ms 后提升优先级 */
    sched->aging_threshold_ms = 100;

    /* 初始化时间片公平调度（Phase 2）*/
    sched->time_slice_ns = 10 * 1000000ULL;  /* 默认 10ms */
    sched->fairness_enabled = false;          /* 默认禁用 */

    /* 初始化异步抢占（Phase 3）*/
    if (coco_preempt_init() != COCO_OK) {
        /* 非致命错误，继续运行 */
    }

    /* 加载 stack map (Phase 11) */
    const char *stackmap_path = getenv("COCO_STACKMAP_PATH");
    if (!stackmap_path) {
        stackmap_path = "output.coco_stackmap";
    }
    sched->stack_map = coco_load_stack_map(stackmap_path);
    /* Note: stack_map may be NULL if file not found - this is non-fatal */

    return sched;
}

void coco_sched_destroy(coco_sched_t *sched) {
    if (!sched) {
        return;
    }

    /* 清理 I/O 多路复用 */
    coco_poll_cleanup(sched);

    /* 清理时间轮 */
    if (sched->timer_wheel) {
        coco_timer_wheel_destroy(sched->timer_wheel);
        sched->timer_wheel = NULL;
    }

    /* 清理信号处理 */
    coco_signal_cleanup();

    /* 清理异步抢占（Phase 3）*/
    coco_preempt_cleanup();

    /* 清理所有协程 */
    for (uint32_t i = 0; i < sched->coro_capacity; i++) {
        coco_coro_t *coro = sched->coro_table[i];
        if (coro) {
            /* 清理 select 状态 */
            coco_select_cleanup(coro);

            /* 释放栈 */
            if (coro->stack_base) {
                if (coro->stack_from_pool && sched->stack_pool) {
                    stack_pool_free(sched->stack_pool, coro->stack_top, coro->stack_size);
                } else {
                    coco_stack_free(coro->stack_top, coro->stack_size);
                }
                coro->stack_base = NULL;
            }
            free(coro);
        }
    }

    /* 销毁栈池 */
    if (sched->stack_pool) {
        stack_pool_destroy(sched->stack_pool);
        sched->stack_pool = NULL;
    }

    /* 释放 stack map (Phase 11) */
    if (sched->stack_map) {
        coco_free_stack_map(sched->stack_map);
        sched->stack_map = NULL;
    }

    free(sched->coro_table);
    free(sched);

    if (g_current_sched == sched) {
        g_current_sched = NULL;
    }
}

/* 检查并执行老化：提升等待时间过长的协程优先级 */
static void apply_aging(coco_sched_t *sched) {
    if (sched->aging_threshold_ms == 0) {
        return;
    }

    uint64_t now = get_timestamp_ms();

    /* 从低优先级开始检查（IDLE -> LOW -> NORMAL）*/
    for (int p = COCO_PRIORITY_COUNT - 1; p > 0; p--) {
        coco_coro_t *coro = sched->ready_heads[p];
        while (coro) {
            coco_coro_t *next = coro->next;

            /* 检查是否等待时间超过阈值 */
            if (coro->ready_timestamp > 0 &&
                (now - coro->ready_timestamp) >= sched->aging_threshold_ms) {

                /* 从当前优先级队列移除 */
                if (coro->prev) {
                    coro->prev->next = coro->next;
                } else {
                    sched->ready_heads[p] = coro->next;
                }
                if (coro->next) {
                    coro->next->prev = coro->prev;
                } else {
                    sched->ready_tails[p] = coro->prev;
                }
                sched->ready_counts[p]--;

                /* 如果队列空了，清除位图对应位 */
                if (sched->ready_heads[p] == NULL) {
                    sched->ready_bitmap &= ~(1U << p);
                }

                /* 提升优先级 */
                coco_priority_t new_prio = (coco_priority_t)(p - 1);
                coro->priority = new_prio;
                coro->ready_timestamp = now;

                /* 加入新优先级队列尾部 */
                coro->prev = sched->ready_tails[new_prio];
                coro->next = NULL;
                if (sched->ready_tails[new_prio]) {
                    sched->ready_tails[new_prio]->next = coro;
                } else {
                    sched->ready_heads[new_prio] = coro;
                }
                sched->ready_tails[new_prio] = coro;
                sched->ready_counts[new_prio]++;

                /* 设置新优先级的位图位 */
                sched->ready_bitmap |= (1U << new_prio);
            }

            coro = next;
        }
    }
}

/* 老化检查间隔：每 N 次出队执行一次老化检查 */
#define AGING_CHECK_INTERVAL 100

/* 时间片检查间隔：每 N 次切换检查一次时间片 */
#define TIME_SLICE_CHECK_INTERVAL 100

/* 出队：从最高优先级队列头部取出（周期性执行老化检查） */
static coco_coro_t *dequeue_ready(coco_sched_t *sched) {
    /* 周期性执行老化检查 */
    if (++sched->dequeue_count >= AGING_CHECK_INTERVAL) {
        sched->dequeue_count = 0;
        apply_aging(sched);
    }

    /* 使用位图快速定位最高优先级非空队列 */
    if (sched->ready_bitmap == 0) {
        return NULL;
    }

    /* __builtin_ctz 返回最低位 1 的位置，即最高优先级 */
    int p = __builtin_ctz(sched->ready_bitmap);
    coco_coro_t *coro = sched->ready_heads[p];
    if (coro) {
        sched->ready_heads[p] = coro->next;
        if (sched->ready_heads[p]) {
            sched->ready_heads[p]->prev = NULL;
        } else {
            sched->ready_tails[p] = NULL;
            /* 队列空了，清除位图对应位 */
            sched->ready_bitmap &= ~(1U << p);
        }
        sched->ready_counts[p]--;
        sched->ready_count--;
        return coro;
    }
    return NULL;
}

/* 切换到协程 */
static void switch_to_coro(coco_sched_t *sched, coco_coro_t *coro) {
    g_current_coro = coro;
    sched->current = coro;
    coro->state = COCO_STATE_RUNNING;

    /* 记录运行开始时间（Phase 2 时间片公平调度）*/
    if (sched->fairness_enabled) {
        coro->runtime_start_ns = coco_get_time_fast();
        coro->time_slice_expired = false;
    }

    /* 初始化遥测（首次切换） */
    if (coro->stack_high_water_mark == 0) {
        coro->stack_high_water_mark = (size_t)coro->stack_top;
    }

    /* 启用异步抢占定时器（Phase 3）*/
    coco_preempt_arm();

    /* 设置溢出恢复点（仅对可增长协程） */
    if (coro->stack_growable) {
        if (coco_set_overflow_checkpoint() == 0) {
            /* 正常执行 */
            coco_ctx_switch(&sched->main_ctx, &coro->ctx);
        } else {
            /* 从栈溢出恢复，coro 状态已由 handler 设置 */
        }
    } else {
        /* 固定栈协程直接切换 */
        coco_ctx_switch(&sched->main_ctx, &coro->ctx);
    }

    /* 禁用抢占定时器（Phase 3）*/
    coco_preempt_disarm();

    /* 切换回调度器后，更新遥测 */
    size_t current_sp = (size_t)coro->ctx.sp;
    if (current_sp < coro->stack_high_water_mark) {
        coro->stack_high_water_mark = current_sp;
    }
}

/* 处理协程返回 */
static void handle_coro_return(coco_sched_t *sched, coco_coro_t *coro) {
    /* 检查时间片到期（Phase 2）*/
    if (sched->fairness_enabled && coro->state == COCO_STATE_RUNNING) {
        uint64_t now = coco_get_time_fast();
        if (now - coro->runtime_start_ns >= sched->time_slice_ns) {
            coro->time_slice_expired = true;
            /* 强制让出：重新入队并标记为 READY */
            enqueue_ready(sched, coro);
            return;
        }
    }

    switch (coro->state) {
        case COCO_STATE_CREATED:
            /* 协程刚创建，不应该在此出现 */
            break;
        case COCO_STATE_RUNNING:
            /* 协程正在运行，不应该在此出现 */
            break;
        case COCO_STATE_READY:
            /* 协程 yield，已重新入队 */
            break;
        case COCO_STATE_WAITING:
            /* 协程等待 I/O 或 channel */
            break;
        case COCO_STATE_DEAD:
            /* 协程已退出，等待清理 */
            sched->coro_count--;
            break;
        case COCO_STATE_OVERFLOW:
            /* 栈溢出（不可恢复），调用错误回调 */
            if (coro->error_cb) {
                coro->error_cb(coro, COCO_ERROR_STACK_OVERFLOW, "Stack overflow detected");
            }
            sched->coro_count--;
            break;
        case COCO_STATE_OVERFLOW_RESUME:
            /* 栈溢出已恢复，重新入队继续执行 */
            enqueue_ready(sched, coro);
            break;
    }
}

int coco_sched_run(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    g_current_sched = sched;
    g_return_ctx = &sched->main_ctx;

    while (sched->coro_count > 0) {
        /* 处理就绪队列 */
        while (sched->ready_count > 0) {
            coco_coro_t *coro = dequeue_ready(sched);
            switch_to_coro(sched, coro);
            handle_coro_return(sched, coro);
        }

        /* 如果还有协程但没有就绪的，等待 I/O 或定时器 */
        if (sched->coro_count > 0 && sched->ready_count == 0) {
            /* 处理定时器 */
            if (sched->timer_wheel) {
                coco_timer_tick(sched->timer_wheel, sched);
            }

            /* 如果定时器唤醒了协程，继续处理 */
            if (sched->ready_count > 0) {
                continue;
            }

            /* 动态计算 timeout 基于最近定时器到期时间 */
            int timeout_ms = -1;  /* 默认无限等待 */
            if (sched->timer_wheel) {
                uint64_t next_expire = coco_timer_wheel_next_expire(sched->timer_wheel);
                if (next_expire > 0) {
                    uint64_t now = get_current_time_ms_internal();
                    if (next_expire > now) {
                        timeout_ms = (int)(next_expire - now);
                        /* 限制最大等待时间为 1 秒，避免长时间阻塞 */
                        if (timeout_ms > 1000) {
                            timeout_ms = 1000;
                        }
                    } else {
                        /* 定时器已到期，不等待 */
                        timeout_ms = 0;
                    }
                }
            }

            /* 等待 I/O 事件 */
            if (sched->poll_fd >= 0) {
                coco_poll_wait(sched, timeout_ms);
            } else if (timeout_ms < 0) {
                /* 无 I/O fd 且无定时器，短暂休眠后重试（避免忙等） */
                usleep(1000);  /* 1ms */
            } else if (timeout_ms > 0) {
                /* 有定时器但无 I/O，等待定时器到期 */
                usleep((useconds_t)(timeout_ms * 1000));
            }
        }
    }

    return COCO_OK;
}

int coco_sched_run_once(coco_sched_t *sched) {
    if (!sched || sched->ready_count == 0) {
        return COCO_ERROR;
    }

    g_current_sched = sched;
    g_return_ctx = &sched->main_ctx;

    coco_coro_t *coro = dequeue_ready(sched);
    switch_to_coro(sched, coro);
    handle_coro_return(sched, coro);

    return COCO_OK;
}

/* === 协程生命周期 API === */

coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size) {
    if (!sched || !entry) {
        return NULL;
    }

    /* 默认栈大小 2KB（与 Go 1.22+ 一致） */
    if (stack_size == 0) {
        stack_size = COCO_DEFAULT_STACK_SIZE;
    }

    /* 分配协程结构 */
    coco_coro_t *coro = calloc(1, sizeof(coco_coro_t));
    if (!coro) {
        return NULL;
    }

    /* 动态栈启用条件：
     * - stack_size < COCO_STACK_FIXED (64KB)：默认启用动态增长
     * - stack_size >= COCO_STACK_FIXED：使用静态栈（不增长）
     * 这意味着默认行为是动态栈，大栈为静态栈
     */
    bool enable_growable = (stack_size < COCO_STACK_FIXED);

    /* 从栈池分配栈 */
    coro->stack_top = stack_pool_alloc(sched->stack_pool, stack_size);
    if (!coro->stack_top) {
        free(coro);
        return NULL;
    }
    coro->stack_base = (void*)((uintptr_t)coro->stack_top - stack_size - 4096);
    coro->stack_size = stack_size;
    coro->stack_from_pool = true;  /* 标记栈来自池 */

    /* 设置动态栈属性 */
    if (enable_growable) {
        coro->stack_growable = true;
        coro->current_stack_size = stack_size;
        coro->max_stack_size = COCO_STACK_MAX_SIZE;
        /* 设置上下文的栈边界用于溢出检测 */
        coro->ctx.stack_base = coro->stack_base;
        coro->ctx.stack_limit = (void*)((uintptr_t)coro->stack_top - 4096);
    } else {
        coro->stack_growable = false;
        coro->current_stack_size = stack_size;
        coro->max_stack_size = stack_size;  /* 固定栈，不增长 */
    }

    /* 初始化上下文 */
    coco_ctx_init(&coro->ctx, coro->stack_top, coro_entry_wrapper, arg);

    /* 设置协程属性 */
    coro->id = sched->next_id++;
    coro->state = COCO_STATE_CREATED;
    coro->entry = entry;
    coro->arg = arg;
    coro->wait_fd = -1;
    coro->stack_high_water_mark = 0;  /* 遥测初始化 */
    coro->priority = COCO_PRIORITY_NORMAL;  /* 默认优先级 */
    coro->ready_timestamp = 0;

    /* 添加到协程池 (ID 超过容量时扩展表) */
    if (coro->id >= sched->coro_capacity) {
        /* Grow coro_table to accommodate new ID */
        uint32_t new_cap = sched->coro_capacity * 2;
        while (new_cap <= coro->id) {
            new_cap *= 2;
        }
        coco_coro_t **new_table = realloc(sched->coro_table, new_cap * sizeof(coco_coro_t *));
        if (new_table) {
            memset(new_table + sched->coro_capacity, 0,
                   (new_cap - sched->coro_capacity) * sizeof(coco_coro_t *));
            sched->coro_table = new_table;
            sched->coro_capacity = new_cap;
        }
    }
    if (coro->id < sched->coro_capacity) {
        sched->coro_table[coro->id] = coro;
    }
    sched->coro_count++;

    /* 入队到就绪队列 */
    enqueue_ready(sched, coro);

    return coro;
}

/**
 * 验证 stack map 是否已加载（用于动态栈协程）
 *
 * @param sched 调度器指针
 * @return COCO_OK 如果 stack map 已加载，COCO_ERROR 如果未加载
 *
 * 对于使用动态栈的协程，必须先调用此函数验证 stack map 已加载，
 * 否则栈增长时将失败。
 */
int coco_validate_stack_map(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    if (sched->stack_map == NULL) {
        return COCO_ERROR;
    }

    return COCO_OK;
}

void coco_exit(coco_coro_t *coro, void *result) {
    if (!coro) {
        return;
    }

    if (!g_current_coro || g_current_coro != coro) {
        /* coco_exit must be called from within the coroutine */
        return;
    }

    coro->state = COCO_STATE_DEAD;
    coro->result = result;

    /* 切换回调度器 */
    g_current_coro = NULL;
    coco_ctx_switch(&coro->ctx, g_return_ctx);
}

void coco_yield(void) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        /* coco_yield called outside coroutine — silently return */
        return;
    }

    /* 仅当协程正在运行时才重新入队（否则可能已被 channel 等机制设置为 WAITING） */
    if (coro->state == COCO_STATE_RUNNING) {
        /* 检查是否在多线程模式下 */
        extern coco_global_sched_t *coco_global_get(void);
        coco_global_sched_t *gs = coco_global_get();

        if (gs && gs->processor_count > 0) {
            /* 多线程模式：放入全局队列 */
            extern int coco_global_runq_put(struct coco_coro *g);
            coco_global_runq_put(coro);
        } else {
            /* 单线程模式：放入本地队列 */
            enqueue_ready(sched, coro);
        }
    }

    /* 切换回调度器 */
    coco_ctx_switch(&coro->ctx, g_return_ctx);
}

void *coco_join(coco_coro_t *coro) {
    if (!coro) {
        return NULL;
    }

    /* 等待协程结束 */
    while (coro->state != COCO_STATE_DEAD) {
        coco_yield();
    }

    return coro->result;
}

void coco_destroy(coco_coro_t *coro) {
    if (!coro) {
        return;
    }

    /* Cleanup select state if coroutine is destroyed while in select */
    coco_select_cleanup(coro);

    if (coro->stack_base) {
        coco_sched_t *sched = g_current_sched;
        if (coro->stack_from_pool && sched && sched->stack_pool) {
            /* 栈来自池，归还给池 */
            stack_pool_free(sched->stack_pool, coro->stack_top, coro->stack_size);
        } else {
            /* 栈是直接 mmap 分配的（增长后或无池），直接 munmap */
            coco_stack_free(coro->stack_top, coro->stack_size);
        }
        coro->stack_base = NULL;
    }
    free(coro);
}

/* === 协程查询 API === */

coco_coro_t *coco_self(void) {
    return g_current_coro;
}

coco_state_t coco_get_state(coco_coro_t *coro) {
    if (!coro) {
        return COCO_STATE_DEAD;
    }
    return coro->state;
}

uint64_t coco_get_id(coco_coro_t *coro) {
    if (!coro) {
        return 0;
    }
    return coro->id;
}

void coco_set_error_cb(coco_coro_t *coro, coco_error_cb cb) {
    if (coro) {
        coro->error_cb = cb;
    }
}

size_t coco_get_stack_usage(coco_coro_t *coro) {
    if (!coro || coro->stack_high_water_mark == 0) {
        return 0;
    }
    return (size_t)coro->stack_top - coro->stack_high_water_mark;
}

void coco_set_priority(coco_coro_t *coro, coco_priority_t priority) {
    if (!coro || priority < 0 || priority >= COCO_PRIORITY_COUNT) {
        return;
    }

    coco_priority_t old_prio = coro->priority;
    if (old_prio == priority) {
        return;
    }

    coro->priority = priority;

    /* 如果协程在就绪队列中，需要重新排队 */
    if (coro->state == COCO_STATE_READY && g_current_sched) {
        coco_sched_t *sched = g_current_sched;

        /* 从旧优先级队列中移除 */
        if (coro->prev) {
            coro->prev->next = coro->next;
        } else {
            sched->ready_heads[old_prio] = coro->next;
        }
        if (coro->next) {
            coro->next->prev = coro->prev;
        } else {
            sched->ready_tails[old_prio] = coro->prev;
        }
        sched->ready_counts[old_prio]--;

        /* 如果旧队列空了，清除位图 */
        if (sched->ready_heads[old_prio] == NULL) {
            sched->ready_bitmap &= ~(1U << old_prio);
        }

        /* 添加到新优先级队列 */
        coro->prev = sched->ready_tails[priority];
        coro->next = NULL;
        if (sched->ready_tails[priority]) {
            sched->ready_tails[priority]->next = coro;
        } else {
            sched->ready_heads[priority] = coro;
        }
        sched->ready_tails[priority] = coro;
        sched->ready_counts[priority]++;

        /* 设置位图 */
        sched->ready_bitmap |= (1U << priority);
    }
}

coco_priority_t coco_get_priority(coco_coro_t *coro) {
    if (!coro) {
        return COCO_PRIORITY_NORMAL;
    }
    return coro->priority;
}

coco_sched_t *coco_sched_get_current(void) {
    return g_current_sched;
}

uint32_t coco_sched_get_stack_map_count(coco_sched_t *sched) {
    if (!sched || !sched->stack_map) {
        return 0;
    }
    return sched->stack_map->num_funcs;
}
int coco_sched_set_fairness(coco_sched_t *sched, bool enabled, uint32_t slice_ms) {
    if (!sched) {
        return COCO_ERROR;
    }

    sched->fairness_enabled = enabled;

    if (slice_ms > 0) {
        sched->time_slice_ns = (uint64_t)slice_ms * 1000000ULL;
    } else {
        /* 默认 10ms */
        sched->time_slice_ns = 10 * 1000000ULL;
    }

    return COCO_OK;
}
