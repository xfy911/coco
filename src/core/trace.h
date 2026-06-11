/**
 * trace.h - Coroutine tracing internal API
 */

#ifndef TRACE_H
#define TRACE_H

#include "../include/coco.h"
#include <stdatomic.h>

void trace_init(void);
void trace_event(coco_trace_event_t event, coco_coro_t *coro);

#endif
