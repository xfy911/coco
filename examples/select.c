/**
 * select.c - Channel select example
 *
 * Demonstrates Go-style select over multiple channels.
 */

#include "coco.h"
#include <stdio.h>
#include <stdlib.h>

static int total = 0;

/* Preload channel with data, then close */
void preload_coro(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;

    static int vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        coco_channel_send(ch, &vals[i]);
    }
    coco_channel_close(ch);
}

/* Select receiver */
void select_coro(void *arg) {
    coco_channel_t **channels = (coco_channel_t **)arg;
    coco_channel_t *ch1 = channels[0];
    coco_channel_t *ch2 = channels[1];

    printf("=== Channel Select Example ===\n\n");

    void *val = NULL;
    coco_select_case_t cases[2];

    /* Receive from ch1 */
    cases[0].chan = ch1;
    cases[0].dir = COCO_SELECT_RECV;
    cases[0].val = &val;

    /* Receive from ch2 */
    cases[1].chan = ch2;
    cases[1].dir = COCO_SELECT_RECV;
    cases[1].val = &val;

    int idx = coco_channel_select(cases, 2, 0, 0);

    if (idx >= 0 && cases[idx].result == COCO_OK && val != NULL) {
        int *data = (int *)val;
        printf("Selected ch%d, received: %d\n", idx + 1, *data);
        total += *data;
    }

    /* Drain remaining data using regular recv */
    while (coco_channel_recv(ch1, &val) == COCO_OK) {
        int *data = (int *)val;
        printf("Drained from ch1: %d\n", *data);
        total += *data;
    }
    while (coco_channel_recv(ch2, &val) == COCO_OK) {
        int *data = (int *)val;
        printf("Drained from ch2: %d\n", *data);
        total += *data;
    }

    printf("\nTotal: %d (expected: 120, two channels x 60)\n", total);
    printf("\n✅ Select example completed\n");
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }

    coco_channel_t *ch1 = coco_channel_create(5);
    coco_channel_t *ch2 = coco_channel_create(5);

    static coco_channel_t *channels[2];
    channels[0] = ch1;
    channels[1] = ch2;

    /* Preload both channels with data */
    coco_create(sched, preload_coro, ch1, 0);
    coco_create(sched, preload_coro, ch2, 0);

    /* Receiver uses select */
    coco_create(sched, select_coro, channels, 0);

    coco_sched_run(sched);

    coco_channel_destroy(ch1);
    coco_channel_destroy(ch2);
    coco_sched_destroy(sched);

    return 0;
}
