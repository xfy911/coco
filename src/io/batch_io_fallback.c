/**
 * batch_io_fallback.c - Batch I/O fallback for non-io_uring platforms
 *
 * IMPORTANT: This fallback executes I/O synchronously and will block
 * the scheduler. Use only for compatibility; for performance, use
 * Linux with io_uring backend.
 *
 * Supports Windows (WSAPoll) and macOS (kqueue).
 */

#include "../coco_internal.h"
#include "io_internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Batch I/O operation types */
typedef enum {
    BATCH_OP_READ,
    BATCH_OP_WRITE
} batch_op_type_t;

/* Individual operation */
typedef struct batch_op {
    batch_op_type_t type;
    int fd;
    void *buf;
    size_t count;
    ssize_t result;
    struct batch_op *next;
} batch_op_t;

/* Batch I/O context */
struct coco_batch_io {
    coco_sched_t *sched;
    coco_coro_t *coro;
    batch_op_t *ops_head;
    batch_op_t *ops_tail;
    uint32_t op_count;
    bool submitted;
    bool cancelled;
};

coco_batch_io_t *coco_batch_begin(coco_sched_t *sched) {
    if (!sched) return NULL;
    
    coco_batch_io_t *batch = calloc(1, sizeof(coco_batch_io_t));
    if (!batch) return NULL;
    
    batch->sched = sched;
    batch->coro = coco_self();
    return batch;
}

int coco_batch_add_read(coco_batch_io_t *batch, int fd, void *buf, size_t count) {
    if (!batch || !buf || batch->submitted) return COCO_ERROR_INVALID;
    if (batch->op_count >= 64) return COCO_ERROR;
    
    batch_op_t *op = malloc(sizeof(batch_op_t));
    if (!op) return COCO_ERROR_NOMEM;
    
    op->type = BATCH_OP_READ;
    op->fd = fd;
    op->buf = buf;
    op->count = count;
    op->result = 0;
    op->next = NULL;
    
    if (batch->ops_tail) {
        batch->ops_tail->next = op;
    } else {
        batch->ops_head = op;
    }
    batch->ops_tail = op;
    batch->op_count++;
    
    return COCO_OK;
}

int coco_batch_add_write(coco_batch_io_t *batch, int fd, const void *buf, size_t count) {
    if (!batch || !buf || batch->submitted) return COCO_ERROR_INVALID;
    if (batch->op_count >= 64) return COCO_ERROR;
    
    batch_op_t *op = malloc(sizeof(batch_op_t));
    if (!op) return COCO_ERROR_NOMEM;
    
    op->type = BATCH_OP_WRITE;
    op->fd = fd;
    op->buf = (void *)buf;
    op->count = count;
    op->result = 0;
    op->next = NULL;
    
    if (batch->ops_tail) {
        batch->ops_tail->next = op;
    } else {
        batch->ops_head = op;
    }
    batch->ops_tail = op;
    batch->op_count++;
    
    return COCO_OK;
}

/* Execute operation synchronously */
static ssize_t batch_op_execute(batch_op_t *op) {
    if (op->type == BATCH_OP_READ) {
        return read(op->fd, op->buf, op->count);
    } else {
        return write(op->fd, op->buf, op->count);
    }
}

int coco_batch_submit(coco_batch_io_t *batch, coco_batch_result_t *results, size_t max_results) {
    if (!batch || !batch->ops_head) return COCO_ERROR_INVALID;
    if (batch->submitted) return COCO_ERROR_INVALID;
    
    batch->submitted = true;
    
    /* WARNING: Synchronous execution blocks the scheduler */
    batch_op_t *op = batch->ops_head;
    int completed = 0;
    
    while (op && completed < (int)max_results) {
        if (batch->cancelled) {
            op->result = COCO_ERROR_CANCELLED;
        } else {
            op->result = batch_op_execute(op);
        }
        
        if (results) {
            results[completed].fd = op->fd;
            results[completed].result = op->result;
        }
        
        op = op->next;
        completed++;
    }
    
    return completed;
}

int coco_batch_cancel(coco_batch_io_t *batch) {
    if (!batch) return COCO_ERROR_INVALID;
    batch->cancelled = true;
    return COCO_OK;
}

void coco_batch_end(coco_batch_io_t *batch) {
    if (!batch) return;
    
    batch_op_t *op = batch->ops_head;
    while (op) {
        batch_op_t *next = op->next;
        free(op);
        op = next;
    }
    
    free(batch);
}
