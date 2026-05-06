/**
 * test_global_sched_mt.c - Multi-threaded scheduler tests
 *
 * Tests the multi-threaded scheduler API:
 *   - coco_global_sched_start/stop lifecycle
 *   - coco_go coroutine creation and dispatch
 *   - coco_global_sched_wait for completion
 *
 * The tests verify API contracts and lifecycle behavior.
 * If the MT worker runtime has issues, the tests will time out
 * gracefully rather than hanging indefinitely.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include "coco.h"

/* Internal headers */
#include "../../src/core/coro_go.h"
#include "../../src/sched/global_sched.h"

/* Test counter */
static _Atomic int g_counter = 0;

/* Simple coroutine entry */
static void simple_entry(void *arg) {
    int *val = (int *)arg;
    atomic_fetch_add(&g_counter, 1);
    if (val) {
        *val = 42;
    }
}

/* Helper: poll with timeout for counter to reach expected value.
   Returns true if reached, false if timed out. */
static bool wait_for_counter(int expected, int timeout_ms) {
    for (int i = 0; i < timeout_ms / 10; i++) {
        if (atomic_load(&g_counter) >= expected) {
            return true;
        }
        usleep(10000);  /* 10ms */
    }
    return false;
}

int main(void) {
    int pass_count = 0;
    int fail_count = 0;

    printf("Multi-threaded scheduler tests:\n");
    fflush(stdout);

    /* Test 1: coco_global_sched_start and stop lifecycle */
    printf("  test_sched_lifecycle... ");
    fflush(stdout);
    {
        int ret = coco_global_sched_start(2);
        if (ret == COCO_OK) {
            coco_global_sched_t *gs = coco_global_get();
            if (gs && coco_processor_count() == 2) {
                ret = coco_global_sched_stop();
                if (ret == COCO_OK) {
                    printf("PASSED\n"); pass_count++;
                } else {
                    printf("FAILED (stop=%d)\n", ret); fail_count++;
                }
            } else {
                printf("FAILED (gs=%p procs=%u)\n", (void*)gs, coco_processor_count());
                fail_count++;
                coco_global_sched_stop();
            }
        } else {
            printf("FAILED (start=%d)\n", ret); fail_count++;
        }
    }

    /* Test 2: coco_go creates coroutines and dispatches to global queue */
    printf("  test_coco_go_dispatch... ");
    fflush(stdout);
    {
        int ret = coco_global_sched_start(2);
        if (ret != COCO_OK) {
            printf("FAILED (start=%d)\n", ret); fail_count++;
            goto done;
        }

        atomic_store(&g_counter, 0);
        int vals[4] = {0};
        int go_ok = 1;
        for (int i = 0; i < 4; i++) {
            coco_coro_t *coro = coco_go(simple_entry, &vals[i]);
            if (!coro) {
                go_ok = 0;
                break;
            }
        }

        if (!go_ok) {
            printf("FAILED (coco_go returned NULL)\n"); fail_count++;
            coco_global_sched_stop();
            goto done;
        }

        /* Verify active_coroutines was incremented */
        coco_global_sched_t *gs = coco_global_get();
        if (atomic_load(&gs->active_coroutines) < 1) {
            printf("FAILED (active_coroutines=%d, expected >=1)\n",
                   (int)atomic_load(&gs->active_coroutines));
            fail_count++;
            coco_global_sched_stop();
            goto done;
        }

        /* Wait for coroutines to complete with timeout.
           If the MT runtime doesn't process coroutines correctly,
           we time out gracefully instead of hanging. */
        bool completed = wait_for_counter(4, 3000);

        if (completed) {
            bool vals_ok = true;
            for (int i = 0; i < 4; i++) {
                if (vals[i] != 42) { vals_ok = false; break; }
            }
            if (vals_ok) {
                printf("PASSED\n"); pass_count++;
            } else {
                printf("FAILED (vals incorrect)\n"); fail_count++;
            }
        } else {
            printf("TIMEOUT (counter=%d, MT runtime may have issues)\n",
                   atomic_load(&g_counter));
            fail_count++;
        }

        coco_global_sched_stop();
    }

    /* Test 3: coco_global_sched_wait with timeout fallback */
    printf("  test_sched_wait... ");
    fflush(stdout);
    {
        int ret = coco_global_sched_start(2);
        if (ret != COCO_OK) {
            printf("SKIPPED (start failed)\n");
            goto done;
        }

        atomic_store(&g_counter, 0);
        coco_go(simple_entry, NULL);

        /* Try coco_global_sched_wait with a fallback timeout */
        bool wait_ok = false;
        for (int i = 0; i < 50; i++) {
            ret = coco_global_sched_wait();
            if (ret == COCO_OK) {
                wait_ok = true;
                break;
            }
            usleep(100000);  /* 100ms */
        }

        if (wait_ok) {
            printf("PASSED\n"); pass_count++;
        } else {
            printf("TIMEOUT\n"); fail_count++;
        }

        coco_global_sched_stop();
    }

done:
    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
