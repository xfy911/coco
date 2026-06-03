#include "../coco_internal.h"
#include "stack_common.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

int coco_hot_slots_init(coco_sched_t *sched, int count) {
    size_t page_size = get_page_size();
    size_t total_size = COCO_HOT_STACK_SIZE + page_size;

    coco_hot_slot_t *slots = calloc(count, sizeof(coco_hot_slot_t));
    if (!slots) return -1;

    for (int i = 0; i < count; i++) {
        void *base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            for (int j = 0; j < i; j++) {
                munmap(slots[j].stack_base, total_size);
            }
            free(slots);
            return -1;
        }

        if (mprotect(base, page_size, PROT_NONE) != 0) {
            munmap(base, total_size);
            for (int j = 0; j < i; j++) {
                munmap(slots[j].stack_base, total_size);
            }
            free(slots);
            return -1;
        }

        slots[i].stack_base = base;
        slots[i].stack_top = (char *)base + total_size;
        slots[i].stack_size = COCO_HOT_STACK_SIZE;
        slots[i].occupant = NULL;
        slots[i].in_use = false;
        slots[i].reserved = false;
    }

    slots[0].reserved = true;

    sched->hot_slots = slots;
    sched->hot_slot_count = count;
    sched->hot_lru_head = NULL;
    sched->hot_lru_tail = NULL;
    sched->hot_coro_count = 0;

    return 0;
}

void coco_hot_slots_destroy(coco_sched_t *sched) {
    if (!sched->hot_slots) return;

    size_t page_size = get_page_size();
    size_t total_size = COCO_HOT_STACK_SIZE + page_size;

    for (int i = 0; i < sched->hot_slot_count; i++) {
        if (sched->hot_slots[i].stack_base) {
            munmap(sched->hot_slots[i].stack_base, total_size);
        }
    }

    free(sched->hot_slots);
    sched->hot_slots = NULL;
    sched->hot_slot_count = 0;
    sched->hot_lru_head = NULL;
    sched->hot_lru_tail = NULL;
    sched->hot_coro_count = 0;
}

void hot_lru_insert_head(coco_sched_t *sched, coco_hot_node_t *node) {
    node->prev = NULL;
    node->next = sched->hot_lru_head;

    if (sched->hot_lru_head) {
        sched->hot_lru_head->prev = node;
    } else {
        sched->hot_lru_tail = node;
    }

    sched->hot_lru_head = node;
}

void hot_lru_move_to_head(coco_sched_t *sched, coco_hot_node_t *node) {
    if (node == sched->hot_lru_head) return;

    hot_lru_remove(sched, node);
    hot_lru_insert_head(sched, node);
}

void hot_lru_remove(coco_sched_t *sched, coco_hot_node_t *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        sched->hot_lru_head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        sched->hot_lru_tail = node->prev;
    }

    node->next = NULL;
    node->prev = NULL;
}
