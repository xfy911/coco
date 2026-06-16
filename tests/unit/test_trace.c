#include "coco.h"
#include <stdio.h>
#include <assert.h>

static int g_trace_count = 0;
static int g_create_seen = 0;
static int g_done_seen = 0;

static void trace_cb(const coco_trace_info_t *info, void *user_data) {
    (void)user_data;
    g_trace_count++;
    
    if (info->event == COCO_TRACE_CORO_CREATE) {
        g_create_seen++;
    } else if (info->event == COCO_TRACE_CORO_DONE) {
        g_done_seen++;
    }
}

static void trace_test_coro(void *arg) {
    (void)arg;
    /* Just run and exit */
}

int main(void) {
    coco_trace_set_callback(trace_cb, NULL);
    
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);
    
    int before_create = g_create_seen;
    int before_done = g_done_seen;
    
    coco_coro_t *coro = coco_create(sched, trace_test_coro, NULL, 0);
    assert(coro != NULL);
    
    coco_sched_run(sched);
    
    assert(g_create_seen > before_create);
    assert(g_done_seen > before_done);
    
    coco_trace_set_callback(NULL, NULL);
    coco_sched_destroy(sched);
    
    printf("All trace tests passed (events: %d)\n", g_trace_count);
    return 0;
}
