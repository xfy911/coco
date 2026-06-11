/**
 * trace.c - Coroutine tracing implementation
 */

#include "trace.h"
#include "../coco_internal.h"
#include <time.h>

static _Atomic(coco_trace_cb) g_trace_cb = NULL;
static _Atomic(void *) g_trace_user_data = NULL;

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void trace_init(void) {
    atomic_store(&g_trace_cb, NULL);
    atomic_store(&g_trace_user_data, NULL);
}

void trace_set_callback(coco_trace_cb cb, void *user_data) {
    atomic_store(&g_trace_cb, cb);
    atomic_store(&g_trace_user_data, user_data);
}

void trace_event(coco_trace_event_t event, coco_coro_t *coro) {
    if (!coro) return;
    
    coco_trace_cb cb = atomic_load(&g_trace_cb);
    if (!cb) return;
    
    coco_trace_info_t info = {
        .event = event,
        .coro_id = coro->id,
        .state = atomic_load_explicit(&coro->state, memory_order_acquire),
        .timestamp_ns = get_timestamp_ns()
    };
    
    void *user_data = atomic_load(&g_trace_user_data);
    cb(&info, user_data);
}

/* Public API */
void coco_trace_set_callback(coco_trace_cb cb, void *user_data) {
    trace_set_callback(cb, user_data);
}
