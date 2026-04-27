/**
 * coro.c - 协程生命周期管理
 */

#include "coco_internal.h"
#include <stdlib.h>
#include <string.h>

/* 全局调度器指针（单线程模式） */
static coco_sched_t *g_current_sched = NULL;
static coco_coro_t *g_current_coro = NULL;

/* 协程入口包装函数 */
static void coro_entry_wrapper(void *arg) {
    coco_coro_t *coro = coco_self();
    if (coro && coro->entry) {
        coro->entry(coro->arg);
    }
    coco_exit(coro, NULL);
}

/* 入队：添加到尾部 */
void enqueue_ready(coco_sched_t *sched, coco_coro_t *coro) {
    coro->state = COCO_STATE_READY;
    coro->prev = sched->ready_tail;
    coro->next = NULL;

    if (sched->ready_tail) {
        sched->ready_tail->next = coro;
    } else {
        sched->ready_head = coro;
    }
    sched->ready_tail = coro;
    sched->ready_count++;
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
    g_current_sched = sched;

    return sched;
}

void coco_sched_destroy(coco_sched_t *sched) {
    if (!sched) {
        return;
    }

    /* 清理所有协程 */
    for (uint32_t i = 0; i < sched->coro_capacity; i++) {
        coco_coro_t *coro = sched->coro_table[i];
        if (coro) {
            if (coro->stack_base) {
                coco_stack_free(coro->stack_top, coro->stack_size);
            }
            free(coro);
        }
    }

    free(sched->coro_table);
    free(sched);

    if (g_current_sched == sched) {
        g_current_sched = NULL;
    }
}

/* 出队：从头部取出 */
static coco_coro_t *dequeue_ready(coco_sched_t *sched) {
    coco_coro_t *coro = sched->ready_head;
    if (!coro) {
        return NULL;
    }

    sched->ready_head = coro->next;
    if (sched->ready_head) {
        sched->ready_head->prev = NULL;
    } else {
        sched->ready_tail = NULL;
    }

    sched->ready_count--;
    return coro;
}

/* 切换到协程 */
static void switch_to_coro(coco_sched_t *sched, coco_coro_t *coro) {
    g_current_coro = coro;
    sched->current = coro;
    coro->state = COCO_STATE_RUNNING;

    coco_ctx_switch(&sched->main_ctx, &coro->ctx);
}

/* 处理协程返回 */
static void handle_coro_return(coco_sched_t *sched, coco_coro_t *coro) {
    switch (coro->state) {
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
            /* 栈溢出，调用错误回调 */
            if (coro->error_cb) {
                coro->error_cb(coro, COCO_ERROR_STACK_OVERFLOW, "Stack overflow detected");
            }
            sched->coro_count--;
            break;
    }
}

int coco_sched_run(coco_sched_t *sched) {
    if (!sched) {
        return COCO_ERROR;
    }

    g_current_sched = sched;

    while (sched->coro_count > 0) {
        /* 处理就绪队列 */
        while (sched->ready_count > 0) {
            coco_coro_t *coro = dequeue_ready(sched);
            switch_to_coro(sched, coro);
            handle_coro_return(sched, coro);
        }

        /* 如果还有协程但没有就绪的，等待 */
        if (sched->coro_count > 0 && sched->ready_count == 0) {
            /* TODO: 等待 I/O 事件或定时器 */
            break;
        }
    }

    return COCO_OK;
}

int coco_sched_run_once(coco_sched_t *sched) {
    if (!sched || sched->ready_count == 0) {
        return COCO_ERROR;
    }

    g_current_sched = sched;

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

    if (stack_size == 0) {
        stack_size = COCO_DEFAULT_STACK_SIZE;
    }

    /* 分配协程结构 */
    coco_coro_t *coro = calloc(1, sizeof(coco_coro_t));
    if (!coro) {
        return NULL;
    }

    /* 分配栈 */
    coro->stack_top = coco_stack_alloc(stack_size);
    if (!coro->stack_top) {
        free(coro);
        return NULL;
    }
    coro->stack_base = (void*)((uintptr_t)coro->stack_top - stack_size - 4096);
    coro->stack_size = stack_size;

    /* 初始化上下文 */
    coco_ctx_init(&coro->ctx, coro->stack_top, coro_entry_wrapper, arg);

    /* 设置协程属性 */
    coro->id = sched->next_id++;
    coro->state = COCO_STATE_CREATED;
    coro->entry = entry;
    coro->arg = arg;
    coro->wait_fd = -1;

    /* 添加到协程池 */
    if (coro->id < sched->coro_capacity) {
        sched->coro_table[coro->id] = coro;
    }
    sched->coro_count++;

    /* 入队到就绪队列 */
    enqueue_ready(sched, coro);

    return coro;
}

void coco_exit(coco_coro_t *coro, void *result) {
    if (!coro) {
        return;
    }

    coro->state = COCO_STATE_DEAD;
    coro->result = result;

    /* 切换回调度器 */
    coco_sched_t *sched = g_current_sched;
    g_current_coro = NULL;
    coco_ctx_switch(&coro->ctx, &sched->main_ctx);
}

void coco_yield(void) {
    coco_sched_t *sched = g_current_sched;
    coco_coro_t *coro = g_current_coro;

    if (!sched || !coro) {
        return;
    }

    /* 重新入队 */
    enqueue_ready(sched, coro);

    /* 切换回调度器 */
    coco_ctx_switch(&coro->ctx, &sched->main_ctx);
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

    if (coro->stack_base) {
        coco_stack_free(coro->stack_top, coro->stack_size);
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