#include "coco.h"
#include "../src/coco_internal.h"
#include <stdio.h>
#include <assert.h>

static coco_channel_t *g_ch = NULL;

static void producer(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    for (int i = 0; i < 100; i++) {
        coco_channel_send(ch, (void *)(intptr_t)i);
    }
    coco_channel_close(ch);
}

static void consumer(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    void *val;
    int count = 0;
    while (coco_channel_recv(ch, &val) == COCO_OK) {
        count++;
    }
    assert(count == 100);
}

static void test_channel_with_preempt(void *arg) {
    (void)arg;
    coco_preempt_enable();
    coco_sched_t *sched = coco_sched_get_current();
    coco_sched_set_fairness(sched, true, 1);
    
    g_ch = coco_channel_create(0);
    assert(g_ch != NULL);
    
    coco_create(sched, producer, g_ch, 0);
    coco_create(sched, consumer, g_ch, 0);
    /* Don't destroy here - main() will destroy after all coroutines finish */
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_create(sched, test_channel_with_preempt, NULL, 0);
    coco_sched_run(sched);
    coco_channel_destroy(g_ch);
    coco_sched_destroy(sched);
    printf("test_preempt_channel: PASSED\n");
    return 0;
}
