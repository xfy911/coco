/**
 * test_hot_stack.c - Hot stack management unit tests
 */

#include "../../src/coco_internal.h"
#include "../../src/core/hot_stack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int test_pass = 0;
static int test_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        test_pass++; \
        printf("  PASS: %s\n", msg); \
    } else { \
        test_fail++; \
        printf("  FAIL: %s\n", msg); \
    } \
} while (0)

static coco_sched_t *create_test_sched(void) {
    coco_sched_t *sched = calloc(1, sizeof(coco_sched_t));
    assert(sched != NULL);
    sched->coro_capacity = 100;
    sched->coro_table = calloc(100, sizeof(coco_coro_t *));
    assert(sched->coro_table != NULL);
    int ret = coco_hot_slots_init(sched, COCO_HOT_SLOTS_DEFAULT);
    assert(ret == 0);
    return sched;
}

static void destroy_test_sched(coco_sched_t *sched) {
    coco_hot_slots_destroy(sched);
    free(sched->coro_table);
    free(sched);
}

static coco_coro_t *create_test_coro(uint64_t id) {
    coco_coro_t *coro = calloc(1, sizeof(coco_coro_t));
    assert(coro != NULL);
    coro->id = id;
    coro->hot_slot_idx = -1;
    coro->hot_node.coro = coro;
    coro->priority = COCO_PRIORITY_NORMAL;
    return coro;
}

static void free_test_coro(coco_coro_t *coro) {
    free(coro->stack_backup);
    free(coro);
}

static void test_hot_slots_init_destroy(void) {
    printf("\n[TEST 1] hot_slots_init / hot_slots_destroy\n");

    coco_sched_t *sched = create_test_sched();

    TEST_ASSERT(sched->hot_slot_count == COCO_HOT_SLOTS_DEFAULT, "slot count is 8");
    TEST_ASSERT(sched->hot_slots[0].reserved == true, "slot 0 is reserved");
    TEST_ASSERT(sched->hot_lru_head == NULL, "LRU head is NULL");
    TEST_ASSERT(sched->hot_lru_tail == NULL, "LRU tail is NULL");
    TEST_ASSERT(sched->hot_coro_count == 0, "hot coro count is 0");

    for (int i = 0; i < sched->hot_slot_count; i++) {
        TEST_ASSERT(sched->hot_slots[i].stack_top != NULL, "stack_top is non-NULL");
        TEST_ASSERT(sched->hot_slots[i].stack_base != NULL, "stack_base is non-NULL");
        TEST_ASSERT(sched->hot_slots[i].in_use == false, "slot not in use");
        TEST_ASSERT(sched->hot_slots[i].occupant == NULL, "no occupant");

        if (i == 0) {
            TEST_ASSERT(sched->hot_slots[i].reserved == true, "slot 0 reserved");
        } else {
            TEST_ASSERT(sched->hot_slots[i].reserved == false, "slot not reserved");
        }
    }

    destroy_test_sched(sched);
    printf("  PASS: destroy completed without crash\n");
    test_pass++;
}

static void test_lru_insert_head(void) {
    printf("\n[TEST 2] LRU insert_head\n");

    coco_sched_t *sched = create_test_sched();

    coco_hot_node_t n1 = {0}, n2 = {0}, n3 = {0};

    hot_lru_insert_head(sched, &n1);
    TEST_ASSERT(sched->hot_lru_head == &n1 && sched->hot_lru_tail == &n1,
                "single node is both head and tail");

    hot_lru_insert_head(sched, &n2);
    hot_lru_insert_head(sched, &n3);

    TEST_ASSERT(sched->hot_lru_head == &n3, "head is n3");
    TEST_ASSERT(sched->hot_lru_tail == &n1, "tail is n1");
    TEST_ASSERT(sched->hot_lru_head->next == &n2, "head->next is n2");
    TEST_ASSERT(sched->hot_lru_head->next->next == &n1, "head->next->next is n1");

    destroy_test_sched(sched);
}

static void test_lru_move_to_head(void) {
    printf("\n[TEST 3] LRU move_to_head\n");

    coco_sched_t *sched = create_test_sched();

    coco_hot_node_t n1 = {0}, n2 = {0}, n3 = {0};

    hot_lru_insert_head(sched, &n1);
    hot_lru_insert_head(sched, &n2);
    hot_lru_insert_head(sched, &n3);

    hot_lru_move_to_head(sched, &n1);

    TEST_ASSERT(sched->hot_lru_head == &n1, "n1 is now head");
    TEST_ASSERT(sched->hot_lru_tail == &n2, "n2 is now tail");
    TEST_ASSERT(sched->hot_lru_head->next == &n3, "head->next is n3");
    TEST_ASSERT(sched->hot_lru_head->next->next == &n2, "n3->next is n2");

    destroy_test_sched(sched);
}

static void test_lru_remove(void) {
    printf("\n[TEST 4] LRU remove\n");

    coco_sched_t *sched = create_test_sched();

    coco_hot_node_t n1 = {0}, n2 = {0}, n3 = {0};

    hot_lru_insert_head(sched, &n1);
    hot_lru_insert_head(sched, &n2);
    hot_lru_insert_head(sched, &n3);

    hot_lru_remove(sched, &n2);

    TEST_ASSERT(sched->hot_lru_head == &n3, "head is n3");
    TEST_ASSERT(sched->hot_lru_tail == &n1, "tail is n1");
    TEST_ASSERT(sched->hot_lru_head->next == &n1, "head->next is n1");
    TEST_ASSERT(n2.next == NULL && n2.prev == NULL, "n2 detached");

    hot_lru_remove(sched, &n3);
    TEST_ASSERT(sched->hot_lru_head == &n1, "head is n1 after removing n3");
    TEST_ASSERT(sched->hot_lru_tail == &n1, "tail is n1 after removing n3");

    hot_lru_remove(sched, &n1);
    TEST_ASSERT(sched->hot_lru_head == NULL, "head is NULL after removing all");
    TEST_ASSERT(sched->hot_lru_tail == NULL, "tail is NULL after removing all");

    destroy_test_sched(sched);
}

static void test_hot_slot_acquire_fills_free(void) {
    printf("\n[TEST 5] hot_slot_acquire fills free slots\n");

    coco_sched_t *sched = create_test_sched();

    TEST_ASSERT(sched->hot_slots[0].reserved == true, "slot 0 is reserved");
    TEST_ASSERT(sched->hot_slots[1].reserved == false, "slot 1 is not reserved");

    coco_coro_t *high_coro = create_test_coro(0);
    high_coro->priority = COCO_PRIORITY_HIGH;
    coco_hot_slot_t *hs = hot_slot_acquire(sched, high_coro);
    TEST_ASSERT(hs == &sched->hot_slots[0], "HIGH coro gets reserved slot 0");
    hs->occupant = high_coro;
    hs->in_use = true;
    high_coro->hot_slot_idx = 0;
    hot_lru_insert_head(sched, &high_coro->hot_node);
    sched->hot_coro_count++;

    coco_coro_t *coros[7];
    for (int i = 0; i < 7; i++) {
        coros[i] = create_test_coro(i + 1);
        coco_hot_slot_t *slot = hot_slot_acquire(sched, coros[i]);
        TEST_ASSERT(slot != NULL, "acquired slot");
        TEST_ASSERT(slot != &sched->hot_slots[0], "not reserved slot");

        slot->occupant = coros[i];
        slot->in_use = true;
        coros[i]->hot_slot_idx = (int)(slot - sched->hot_slots);
        hot_lru_insert_head(sched, &coros[i]->hot_node);
        sched->hot_coro_count++;
    }

    bool all_in_use = true;
    for (int i = 0; i < sched->hot_slot_count; i++) {
        if (!sched->hot_slots[i].in_use) all_in_use = false;
    }
    TEST_ASSERT(all_in_use, "all slots in use");
    TEST_ASSERT(sched->hot_coro_count == 8, "hot coro count is 8");

    free_test_coro(high_coro);
    for (int i = 0; i < 7; i++) {
        free_test_coro(coros[i]);
    }
    destroy_test_sched(sched);
}

static void test_hot_slot_acquire_evicts_when_full(void) {
    printf("\n[TEST 6] hot_slot_acquire evicts when full\n");

    coco_sched_t *sched = create_test_sched();

    coco_coro_t *high_coro = create_test_coro(0);
    high_coro->priority = COCO_PRIORITY_HIGH;
    high_coro->stack_used = 64;
    coco_hot_slot_t *hs = hot_slot_acquire(sched, high_coro);
    assert(hs == &sched->hot_slots[0]);
    hs->occupant = high_coro;
    hs->in_use = true;
    high_coro->hot_slot_idx = 0;
    hot_lru_insert_head(sched, &high_coro->hot_node);
    sched->hot_coro_count++;

    coco_coro_t *coros[7];
    for (int i = 0; i < 7; i++) {
        coros[i] = create_test_coro(i + 1);
        coros[i]->stack_used = 64;
        coco_hot_slot_t *slot = hot_slot_acquire(sched, coros[i]);
        assert(slot != NULL);

        slot->occupant = coros[i];
        slot->in_use = true;
        coros[i]->hot_slot_idx = (int)(slot - sched->hot_slots);
        hot_lru_insert_head(sched, &coros[i]->hot_node);
        sched->hot_coro_count++;
    }

    TEST_ASSERT(sched->hot_coro_count == 8, "8 slots occupied");

    coco_coro_t *extra = create_test_coro(9);
    extra->stack_used = 64;
    coco_hot_slot_t *slot9 = hot_slot_acquire(sched, extra);
    TEST_ASSERT(slot9 != NULL, "acquired slot via eviction");

    coco_coro_t *evicted = NULL;
    for (int i = 0; i < 7; i++) {
        if (coros[i]->hot_slot_idx == -1) {
            evicted = coros[i];
            break;
        }
    }
    TEST_ASSERT(evicted != NULL, "a coro was evicted");
    TEST_ASSERT(evicted->stack_backup != NULL, "evicted coro has stack backup");

    free_test_coro(high_coro);
    for (int i = 0; i < 7; i++) {
        free_test_coro(coros[i]);
    }
    free_test_coro(extra);
    destroy_test_sched(sched);
}

static void test_hot_slot_release(void) {
    printf("\n[TEST 7] hot_slot_release\n");

    coco_sched_t *sched = create_test_sched();
    coco_coro_t *coro = create_test_coro(1);

    coco_hot_slot_t *slot = hot_slot_acquire(sched, coro);
    assert(slot != NULL);

    int slot_idx = (int)(slot - sched->hot_slots);
    slot->occupant = coro;
    slot->in_use = true;
    coro->hot_slot_idx = slot_idx;
    hot_lru_insert_head(sched, &coro->hot_node);
    sched->hot_coro_count++;

    TEST_ASSERT(sched->hot_slots[slot_idx].in_use == true, "slot is in use before release");

    hot_slot_release(sched, coro);

    TEST_ASSERT(sched->hot_slots[slot_idx].in_use == false, "slot is free after release");
    TEST_ASSERT(sched->hot_slots[slot_idx].occupant == NULL, "slot has no occupant");
    TEST_ASSERT(coro->hot_slot_idx == -1, "coro is cold");
    TEST_ASSERT(sched->hot_coro_count == 0, "hot coro count is 0");

    free_test_coro(coro);
    destroy_test_sched(sched);
}

static void test_priority_reserved_slot(void) {
    printf("\n[TEST 8] priority reserved slot\n");

    coco_sched_t *sched = create_test_sched();

    coco_coro_t *high_coro = create_test_coro(1);
    high_coro->priority = COCO_PRIORITY_HIGH;

    coco_hot_slot_t *slot = hot_slot_acquire(sched, high_coro);
    TEST_ASSERT(slot != NULL, "HIGH priority acquired slot");
    TEST_ASSERT(slot == &sched->hot_slots[0], "HIGH priority got reserved slot 0");

    slot->occupant = high_coro;
    slot->in_use = true;
    high_coro->hot_slot_idx = 0;
    hot_lru_insert_head(sched, &high_coro->hot_node);
    sched->hot_coro_count++;

    coco_coro_t *normal_coros[7];
    for (int i = 0; i < 7; i++) {
        normal_coros[i] = create_test_coro(i + 10);
        coco_hot_slot_t *ns = hot_slot_acquire(sched, normal_coros[i]);
        assert(ns != NULL);
        ns->occupant = normal_coros[i];
        ns->in_use = true;
        normal_coros[i]->hot_slot_idx = (int)(ns - sched->hot_slots);
        hot_lru_insert_head(sched, &normal_coros[i]->hot_node);
        sched->hot_coro_count++;
    }

    coco_coro_t *normal_last = create_test_coro(99);
    normal_last->stack_used = 64;
    coco_hot_slot_t *ns = hot_slot_acquire(sched, normal_last);
    TEST_ASSERT(ns != NULL, "NORMAL coro got slot via eviction");

    int ns_idx = (int)(ns - sched->hot_slots);
    TEST_ASSERT(ns_idx != 0, "NORMAL coro did not get reserved slot 0");

    free_test_coro(high_coro);
    for (int i = 0; i < 7; i++) {
        free_test_coro(normal_coros[i]);
    }
    free_test_coro(normal_last);
    destroy_test_sched(sched);
}

int main(void) {
    printf("=== Hot Stack Management Tests ===\n");

    test_hot_slots_init_destroy();
    test_lru_insert_head();
    test_lru_move_to_head();
    test_lru_remove();
    test_hot_slot_acquire_fills_free();
    test_hot_slot_acquire_evicts_when_full();
    test_hot_slot_release();
    test_priority_reserved_slot();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", test_pass);
    printf("Failed: %d\n", test_fail);

    if (test_fail == 0) {
        printf("\nAll tests passed.\n");
        return 0;
    } else {
        printf("\nSome tests failed.\n");
        return 1;
    }
}
