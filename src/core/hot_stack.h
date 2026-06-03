#ifndef HOT_STACK_H
#define HOT_STACK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define COCO_HOT_SLOTS_DEFAULT   8
#define COCO_HOT_STACK_SIZE      (128 * 1024)
#define COCO_HOT_STACK_THRESHOLD (100 * 1024)

struct coco_coro;
struct coco_sched;

typedef struct coco_hot_slot {
    void *stack_top;
    void *stack_base;
    size_t stack_size;
    struct coco_coro *occupant;
    bool in_use;
    bool reserved;
} coco_hot_slot_t;

typedef struct coco_hot_node {
    struct coco_coro *coro;
    struct coco_hot_node *next;
    struct coco_hot_node *prev;
} coco_hot_node_t;

int coco_hot_slots_init(struct coco_sched *sched, int count);
void coco_hot_slots_destroy(struct coco_sched *sched);

void hot_lru_insert_head(struct coco_sched *sched, coco_hot_node_t *node);
void hot_lru_move_to_head(struct coco_sched *sched, coco_hot_node_t *node);
void hot_lru_remove(struct coco_sched *sched, coco_hot_node_t *node);

struct coco_hot_slot *hot_slot_acquire(struct coco_sched *sched, struct coco_coro *coro);
void hot_slot_release(struct coco_sched *sched, struct coco_coro *coro);

int coro_migrate_to_exclusive(struct coco_sched *sched, struct coco_coro *coro);
void coro_migrate_prepare(struct coco_sched *from, struct coco_coro *coro);

void coco_hot_read_sp(void **sp);

#endif
