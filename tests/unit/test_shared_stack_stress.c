/**
 * test_shared_stack_stress.c - Stress tests for shared stack mode
 */

#include "../../src/coco_internal.h"
#include "../../src/core/hot_stack.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

#define STACK_SIZE (64 * 1024)

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { test_count++; printf("  TEST: %s ... ", name); } while (0)
#define PASS() do { pass_count++; printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void yield_stress_coro(void *arg) {
    int count = *(int *)arg;
    volatile int x = 0;
    for (int i = 0; i < count; i++) {
        x += i;
        coco_yield();
    }
}

static int test_yield_stress(void) {
    TEST("yield stress (100 coros x 100 yields)");
    coco_sched_t *sched = coco_sched_create();
    int count = 100;

    for (int i = 0; i < 100; i++) {
        if (!coco_create(sched, yield_stress_coro, &count, STACK_SIZE)) {
            FAIL("create failed");
            coco_sched_destroy(sched);
            return 1;
        }
    }

    int rc = coco_sched_run(sched);
    coco_sched_destroy(sched);

    if (rc != COCO_OK) { FAIL("sched_run failed"); return 1; }
    PASS();
    return 0;
}

static void channel_sender(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    for (int i = 0; i < 100; i++) {
        coco_channel_send(ch, (void *)(intptr_t)i);
        coco_yield();
    }
    coco_channel_close(ch);
}

static void channel_receiver(void *arg) {
    coco_channel_t *ch = (coco_channel_t *)arg;
    void *val;
    while (coco_channel_recv(ch, &val) == COCO_OK) {
        coco_yield();
    }
}

static int test_channel_stress(void) {
    TEST("channel stress (20 pairs)");
    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *channels[20];

    for (int i = 0; i < 20; i++) {
        channels[i] = coco_channel_create(10);
        coco_create(sched, channel_sender, channels[i], STACK_SIZE);
        coco_create(sched, channel_receiver, channels[i], STACK_SIZE);
    }

    int rc = coco_sched_run(sched);
    for (int i = 0; i < 20; i++) coco_channel_destroy(channels[i]);
    coco_sched_destroy(sched);

    if (rc != COCO_OK) { FAIL("sched_run failed"); return 1; }
    PASS();
    return 0;
}

static void recursive_coro(void *arg) {
    int depth = *(int *)arg;
    if (depth <= 0) { coco_yield(); return; }
    int next = depth - 1;
    recursive_coro(&next);
}

static int test_recursion(void) {
    TEST("recursion on shared stack (depth 50)");
    coco_sched_t *sched = coco_sched_create();
    int depth = 50;

    if (!coco_create(sched, recursive_coro, &depth, STACK_SIZE)) {
        FAIL("create failed");
        coco_sched_destroy(sched);
        return 1;
    }

    int rc = coco_sched_run(sched);
    coco_sched_destroy(sched);

    if (rc != COCO_OK) { FAIL("sched_run failed"); return 1; }
    PASS();
    return 0;
}

int main(void) {
    printf("=== Shared Stack Stress Tests ===\n\n");
    int fails = 0;
    fails += test_yield_stress();
    fails += test_channel_stress();
    fails += test_recursion();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return fails;
}
