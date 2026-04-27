/**
 * coro.c - 协程生命周期管理
 */

#include "coco.h"

coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size) {
    return NULL;
}

void coco_exit(coco_coro_t *coro, void *result) {
}

void coco_yield(void) {
}

void *coco_join(coco_coro_t *coro) {
    return NULL;
}

void coco_destroy(coco_coro_t *coro) {
}

coco_coro_t *coco_self(void) {
    return NULL;
}

coco_state_t coco_get_state(coco_coro_t *coro) {
    return COCO_STATE_DEAD;
}

uint64_t coco_get_id(coco_coro_t *coro) {
    return 0;
}

void coco_set_error_cb(coco_coro_t *coro, coco_error_cb cb) {
}