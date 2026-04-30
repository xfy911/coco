/**
 * safety.c - Safety mode implementation for dynamic stack growth
 *
 * @file safety.c
 * @brief Safety mode configuration and management
 */

#include "coco_safety.h"
#include "coco_stack_grow.h"
#include "coco_stack_map.h"
#include "coco_frame_walker.h"
#include "../coco_internal.h"

#include <stdlib.h>
#include <string.h>

/* Global safety mode */
static coco_safety_mode_t g_safety_mode = COCO_SAFETY_NONE;

/**
 * Get default configuration for a safety mode.
 */
coco_safety_config_t coco_get_default_config(coco_safety_mode_t mode) {
    coco_safety_config_t config = {0};
    config.mode = mode;

    switch (mode) {
        case COCO_SAFETY_NONE:
            config.min_stack_size = COCO_STACK_DEFAULT_SIZE;
            config.max_stack_size = COCO_STACK_DEFAULT_SIZE;
            config.grow_threshold_percent = 100;  // Never grow
            config.shrink_threshold_percent = 0;
            config.scan_all_pointers = false;
            config.auto_shrink = false;
            break;

        case COCO_SAFETY_CONSERVATIVE:
            config.min_stack_size = COCO_STACK_MIN_SIZE;
            config.max_stack_size = COCO_STACK_CONSERVATIVE_MAX;  // 64KB limit
            config.grow_threshold_percent = 75;   // Grow at 75% usage
            config.shrink_threshold_percent = 0;  // Never shrink
            config.scan_all_pointers = false;
            config.auto_shrink = false;
            break;

        case COCO_SAFETY_FULL:
            config.min_stack_size = COCO_STACK_MIN_SIZE;
            config.max_stack_size = COCO_STACK_MAX_SIZE;
            config.grow_threshold_percent = 75;
            config.shrink_threshold_percent = 25; // Shrink below 25% usage
            config.scan_all_pointers = true;
            config.auto_shrink = true;
            break;

        default:
            config.min_stack_size = COCO_STACK_DEFAULT_SIZE;
            config.max_stack_size = COCO_STACK_DEFAULT_SIZE;
            break;
    }

    return config;
}

/**
 * Set global safety mode.
 */
coco_safety_mode_t coco_set_safety_mode(coco_safety_mode_t mode) {
    coco_safety_mode_t prev = g_safety_mode;
    g_safety_mode = mode;
    return prev;
}

/**
 * Get current global safety mode.
 */
coco_safety_mode_t coco_get_safety_mode(void) {
    return g_safety_mode;
}

/**
 * Create a coroutine with specific safety mode.
 */
coco_coro_t* coco_create_safe(
    coco_sched_t* sched,
    void (*entry)(void*),
    void* arg,
    size_t stack_size,
    coco_safety_mode_t mode
) {
    if (!sched || !entry) {
        return NULL;
    }

    coco_safety_config_t config = coco_get_default_config(mode);

    /* Select initial stack size based on mode */
    if (stack_size == 0) {
        stack_size = config.min_stack_size;
    }

    /* Create coroutine using standard path */
    coco_coro_t* coro = coco_create(sched, entry, arg, stack_size);
    if (!coro) {
        return NULL;
    }

    /* Configure dynamic stack growth for non-NONE modes */
    if (mode != COCO_SAFETY_NONE) {
        coro->safety_mode = mode;
        coro->max_stack_size = config.max_stack_size;
        coro->stack_growable = true;
        coro->current_stack_size = stack_size;

        /* Initialize stack bounds in context */
        coro->ctx.stack_base = coro->stack_base;
        coro->ctx.stack_limit = (void*)((uintptr_t)coro->stack_top - stack_size);
    } else {
        coro->safety_mode = COCO_SAFETY_NONE;
        coro->max_stack_size = stack_size;  /* Fixed size */
        coro->stack_growable = false;
        coro->current_stack_size = stack_size;
    }

    return coro;
}

/**
 * Check if a coroutine can shrink its stack.
 */
bool coco_can_shrink_stack(
    const coco_coro_t* coro,
    const coco_safety_config_t* config
) {
    if (!config->auto_shrink) {
        return false;
    }

    uintptr_t stack_base = (uintptr_t)coro->ctx.stack_base;
    uintptr_t stack_limit = (uintptr_t)coro->ctx.stack_limit;
    uintptr_t current_sp = (uintptr_t)coro->ctx.sp;

    if (stack_base == 0 || stack_limit == 0) {
        return false;
    }

    size_t stack_size = stack_limit - stack_base;
    size_t used = stack_limit - current_sp;  // Stack grows downward

    // Shrink when usage below threshold
    uint8_t threshold = config->shrink_threshold_percent;
    size_t min_used = stack_size * threshold / 100;

    // Don't shrink below minimum size
    if (stack_size / 2 < config->min_stack_size) {
        return false;
    }

    return used < min_used;
}

/**
 * Shrink a coroutine's stack.
 */
bool coco_shrink_stack(
    coco_coro_t* coro,
    const coco_stack_map_t* stack_map,
    const coco_safety_config_t* config
) {
    if (!coco_can_shrink_stack(coro, config)) {
        return false;
    }

    uintptr_t old_base = (uintptr_t)coro->ctx.stack_base;
    uintptr_t old_limit = (uintptr_t)coro->ctx.stack_limit;
    size_t old_size = old_limit - old_base;

    // Shrink to half size
    size_t new_size = old_size / 2;
    if (new_size < config->min_stack_size) {
        new_size = config->min_stack_size;
    }

    // Similar to growth but smaller
    // For simplicity, we don't implement shrinking in this phase
    // Full implementation would allocate smaller stack, copy, adjust

    return false;  // Not implemented yet
}

/**
 * Scan stack for potential pointers (Level 2 safety).
 */
uint32_t coco_scan_stack_pointers(
    uintptr_t stack_base,
    uintptr_t stack_limit,
    uintptr_t current_sp,
    coco_pointer_visitor_fn visitor,
    void* user_data
) {
    uint32_t count = 0;

    // Conservative scan: every 8-byte aligned address
    // within the used portion of the stack
    uintptr_t scan_start = current_sp;
    uintptr_t scan_end = stack_limit;

    // Align to 8 bytes
    scan_start = (scan_start + 7) & ~7;

    for (uintptr_t addr = scan_start; addr < scan_end; addr += 8) {
        uintptr_t value = *(uintptr_t*)addr;

        // Heuristic: check if value looks like a stack pointer
        // - Within stack bounds
        // - Properly aligned
        if (value >= stack_base && value < stack_limit) {
            // Potential pointer found

            // Create a fake descriptor
            coco_ptr_desc_t desc = {
                .frame_offset = (int32_t)(addr - current_sp),
                .size = 8,
                .kind = COCO_PTR_MAYBE_PTR,
                .flags = 0
            };

            // Call visitor
            if (!visitor(addr, value, &desc, NULL, user_data)) {
                break;
            }
            count++;
        }
    }

    return count;
}