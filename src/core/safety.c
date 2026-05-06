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
#include <unistd.h>
#include <sys/mman.h>

/* Global safety mode */
coco_safety_mode_t g_safety_mode = COCO_SAFETY_NONE;

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
    if (!coro || !config) {
        return false;
    }

    if (!coro->stack_base || !coro->stack_top || coro->stack_size == 0) {
        return false;
    }

    if (!coco_can_shrink_stack(coro, config)) {
        return false;
    }

    /* Shrink to half size, bounded by min_stack_size */
    size_t new_size = coro->stack_size / 2;
    if (new_size < config->min_stack_size) {
        return false;
    }

    /* Page-align new size */
    size_t page_size = sysconf(_SC_PAGESIZE);
    new_size = ((new_size + page_size - 1) / page_size) * page_size;

    /* Allocate new stack with guard page */
    void *new_alloc = mmap(NULL, new_size + page_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (new_alloc == MAP_FAILED) {
        return false;
    }

    /* Set guard page at bottom (lowest address) */
    mprotect(new_alloc, page_size, PROT_NONE);

    uintptr_t new_base = (uintptr_t)new_alloc + page_size;
    uintptr_t new_limit = new_base + new_size;

    /* Copy active stack region (from current SP to old_limit) */
    uintptr_t current_sp = (uintptr_t)coro->ctx.sp;
    uintptr_t old_base = (uintptr_t)coro->ctx.stack_base;
    uintptr_t old_limit = (uintptr_t)coro->ctx.stack_limit;
    size_t active_size = old_limit - current_sp;

    if (active_size > new_size) {
        munmap(new_alloc, new_size + page_size);
        return false;
    }

    /* Copy preserving offset from limit (stack grows down) */
    size_t sp_offset = old_limit - current_sp;
    uintptr_t new_sp = new_limit - sp_offset;
    memcpy((void *)new_sp, (void *)current_sp, active_size);

    /* Calculate delta for pointer adjustment */
    ptrdiff_t delta = (ptrdiff_t)(new_base - old_base);

    /* Adjust saved FP and SP */
    uintptr_t saved_fp = (uintptr_t)coro->ctx.fp;
    uintptr_t saved_sp = (uintptr_t)coro->ctx.sp;

    coco_adjust_frame_pointers(old_base, old_limit - old_base, new_base,
                               &saved_fp, &saved_sp);

    /* Adjust interior pointers using stack map */
    if (stack_map) {
        coco_adjust_stack_pointers(old_base, old_limit, new_base, new_limit,
                                   stack_map, saved_fp, saved_sp);
    }

    /* Free old stack */
    void *old_alloc = (void *)(old_base - page_size);
    size_t old_total = (old_limit - old_base) + page_size;
    munmap(old_alloc, old_total);

    /* Update coroutine fields */
    coro->ctx.stack_base = (void *)new_base;
    coro->ctx.stack_limit = (void *)new_limit;
    coro->ctx.fp = (void *)saved_fp;
    coro->ctx.sp = (void *)saved_sp;
    coro->stack_base = (void *)new_base;
    coro->stack_top = (void *)new_limit;
    coro->stack_size = new_size;
    coro->current_stack_size = new_size;

    return true;
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