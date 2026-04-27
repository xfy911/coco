/**
 * channel.c - Channel 实现
 */

#include "coco.h"

coco_channel_t *coco_channel_create(size_t capacity) {
    return NULL;
}

int coco_channel_send(coco_channel_t *ch, void *value) {
    return COCO_ERROR;
}

int coco_channel_recv(coco_channel_t *ch, void **value) {
    return COCO_ERROR;
}

void coco_channel_close(coco_channel_t *ch) {
}

void coco_channel_destroy(coco_channel_t *ch) {
}