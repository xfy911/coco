/**
 * debug.c - Debug dump utilities
 */

#include "debug.h"
#include "../coco_internal.h"
#include <stdio.h>

static const char *backend_name(coco_poll_backend_t backend) {
    switch (backend) {
        case COCO_POLL_EPOLL: return "epoll";
        case COCO_POLL_KQUEUE: return "kqueue";
        case COCO_POLL_IOURING: return "io_uring";
        case COCO_POLL_WSAPOLL: return "WSAPoll";
        default: return "unknown";
    }
}

static const char *state_name(coco_state_t state) {
    switch (state) {
        case COCO_STATE_CREATED: return "CREATED";
        case COCO_STATE_RUNNING: return "RUNNING";
        case COCO_STATE_WAITING: return "WAITING";
        case COCO_STATE_READY: return "READY";
        case COCO_STATE_DEAD: return "DEAD";
        case COCO_STATE_OVERFLOW: return "OVERFLOW";
        case COCO_STATE_OVERFLOW_RESUME: return "OVERFLOW_RESUME";
        default: return "UNKNOWN";
    }
}

void coco_debug_dump_scheduler(coco_sched_t *sched, FILE *fp) {
    if (!fp) fp = stderr;
    if (!sched) {
        fprintf(fp, "Scheduler: NULL\n");
        return;
    }
    
    fprintf(fp, "=== Scheduler Dump ===\n");
    fprintf(fp, "Coroutine count: %u\n", sched->coro_count);
    fprintf(fp, "Ready count: %u\n", sched->ready_count);
    fprintf(fp, "Next ID: %lu\n", sched->next_id);
    fprintf(fp, "Poll backend: %s\n", backend_name(sched->poll_backend));
    fprintf(fp, "Fairness: %s\n", sched->fairness_enabled ? "enabled" : "disabled");
    
    /* Dump ready queues */
    for (int p = 0; p < COCO_PRIORITY_COUNT; p++) {
        uint32_t count = sched->ready_counts[p];
        if (count > 0) {
            fprintf(fp, "  Priority %d: %u coroutines\n", p, count);
        }
    }
    
    /* Dump all coroutines */
    fprintf(fp, "\n=== Coroutines ===\n");
    for (uint32_t i = 0; i < sched->coro_capacity; i++) {
        coco_coro_t *coro = sched->coro_table[i];
        if (coro) {
            coco_state_t state = coco_get_state(coro);
            fprintf(fp, "  Coro %lu: state=%s prio=%d fd=%d\n",
                   coro->id, state_name(state), coro->priority, coro->wait_fd);
        }
    }
    fprintf(fp, "=== End Dump ===\n");
}
