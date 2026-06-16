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

void coco_hot_read_sp(void **sp) {
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile ("mov %%rsp, %0" : "=r"(*sp));
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile ("mov %0, sp" : "=r"(*sp));
#else
    *sp = NULL;
#endif
}

static coco_hot_slot_t *find_free_slot(coco_sched_t *sched, coco_coro_t *coro) {
    if (coro->priority == COCO_PRIORITY_HIGH) {
        if (!sched->hot_slots[0].in_use) return &sched->hot_slots[0];
    }
    for (int i = 0; i < sched->hot_slot_count; i++) {
        coco_hot_slot_t *slot = &sched->hot_slots[i];
        if (slot->reserved) continue;
        if (!slot->in_use) return slot;
    }
    return NULL;
}

static coco_hot_slot_t *find_eviction_victim_slot(coco_sched_t *sched, coco_coro_t *coro) {
    coco_hot_node_t *node = sched->hot_lru_tail;
    while (node) {
        coco_coro_t *victim = node->coro;
        if (victim == coro) {
            node = node->prev;
            continue;
        }
        coco_hot_slot_t *slot = &sched->hot_slots[victim->hot_slot_idx];
        if (slot->reserved && coro->priority != COCO_PRIORITY_HIGH) {
            node = node->prev;
            continue;
        }
        return slot;
    }
    return NULL;
}

static void backup_coro_stack(coco_coro_t *coro, coco_hot_slot_t *slot) {
    if (coro->stack_used == 0) return;
    void *src = (char *)slot->stack_top - coro->stack_used;
    if (coro->stack_backup_size < coro->stack_used) {
        /* 按 2x 增长预分配，最小 8KB，减少 realloc 次数 */
        size_t new_size = coro->stack_backup_size ? coro->stack_backup_size * 2 : (8 * 1024);
        if (new_size < coro->stack_used) new_size = coro->stack_used;
        void *new_backup = realloc(coro->stack_backup, new_size);
        if (!new_backup) return;
        coro->stack_backup = new_backup;
        coro->stack_backup_size = new_size;
    }
    memcpy(coro->stack_backup, src, coro->stack_used);
}

coco_hot_slot_t *hot_slot_acquire(coco_sched_t *sched, coco_coro_t *coro) {
    coco_hot_slot_t *slot = find_free_slot(sched, coro);
    if (slot) return slot;

    coco_hot_slot_t *victim_slot = find_eviction_victim_slot(sched, coro);
    if (!victim_slot) return NULL;

    coco_coro_t *victim = victim_slot->occupant;
    backup_coro_stack(victim, victim_slot);
    victim->hot_slot_idx = -1;
    hot_lru_remove(sched, &victim->hot_node);
    sched->hot_coro_count--;
    victim_slot->occupant = NULL;
    victim_slot->in_use = false;

    return victim_slot;
}

void hot_slot_release(coco_sched_t *sched, coco_coro_t *coro) {
    if (!coro || coro->hot_slot_idx < 0) return;

    coco_hot_slot_t *slot = &sched->hot_slots[coro->hot_slot_idx];
    slot->in_use = false;
    slot->occupant = NULL;
    hot_lru_remove(sched, &coro->hot_node);
    sched->hot_coro_count--;
    coro->hot_slot_idx = -1;
}

int coro_migrate_to_exclusive(coco_sched_t *sched, coco_coro_t *coro) {
    if (!coro || coro->hot_slot_idx < 0) return COCO_ERROR;

    coco_hot_slot_t *slot = &sched->hot_slots[coro->hot_slot_idx];

    size_t page_size = get_page_size();
    size_t stack_size = COCO_STACK_CONSERVATIVE;
    if (coro->stack_used > stack_size) {
        stack_size = (coro->stack_used + page_size - 1) & ~(page_size - 1);
    }
    size_t total_size = stack_size + page_size;

    void *base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return COCO_ERROR_NOMEM;

    if (mprotect(base, page_size, PROT_NONE) != 0) {
        munmap(base, total_size);
        return COCO_ERROR_NOMEM;
    }

    void *dst_top = (char *)base + total_size;

    if (coro->stack_used > 0) {
        void *src = (char *)slot->stack_top - coro->stack_used;
        void *dst = (char *)dst_top - coro->stack_used;
        memcpy(dst, src, coro->stack_used);
    }

    slot->in_use = false;
    slot->occupant = NULL;
    hot_lru_remove(sched, &coro->hot_node);
    sched->hot_coro_count--;
    coro->hot_slot_idx = -1;

    coro->is_exclusive = true;
    coro->stack_base = (char *)base + page_size;
    coro->stack_top = dst_top;
    coro->stack_size = stack_size;
    coro->stack_from_pool = false;
    coro->stack_growable = true;
    coro->current_stack_size = stack_size;
    coro->max_stack_size = COCO_STACK_MAX_SIZE;

    ptrdiff_t delta = (char *)dst_top - (char *)slot->stack_top;
    coro->ctx.sp = (char *)coro->ctx.sp + delta;
    if (coro->ctx.fp) {
        coro->ctx.fp = (char *)coro->ctx.fp + delta;
    }
    coro->ctx.stack_base = (char *)base + page_size;
    coro->ctx.stack_limit = (char *)base + page_size;

    free(coro->stack_backup);
    coro->stack_backup = NULL;

    return COCO_OK;
}

void coro_migrate_prepare(coco_sched_t *from, coco_coro_t *coro) {
    if (!coro || !from || !from->hot_slots) return;
    if (coro->is_exclusive) return;
    if (coro->hot_slot_idx < 0 || coro->hot_slot_idx >= from->hot_slot_count) return;

    coco_hot_slot_t *slot = &from->hot_slots[coro->hot_slot_idx];
    backup_coro_stack(coro, slot);

    slot->in_use = false;
    slot->occupant = NULL;
    hot_lru_remove(from, &coro->hot_node);
    from->hot_coro_count--;
    coro->hot_slot_idx = -1;
}
