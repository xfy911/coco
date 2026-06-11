#include "coco.h"
#include <stdio.h>
#include <assert.h>

static void debug_test_coro(void *arg) {
    (void)arg;
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    assert(sched != NULL);
    
    coco_create(sched, debug_test_coro, NULL, 0);
    
    /* Just verify it doesn't crash */
    printf("=== Debug Dump Test ===\n");
    coco_debug_dump_scheduler(sched, stdout);
    printf("========================\n");
    
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    
    printf("All debug tests passed\n");
    return 0;
}
