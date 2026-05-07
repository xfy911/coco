/**
 * stack_grow.c - Dynamic stack growth implementation
 *
 * @file stack_grow.c
 * @brief Stack growth runtime for coroutines
 */

#include "coco_stack_grow.h"
#include "coco_stack_map.h"
#include "coco_frame_walker.h"
#include "stack_pool.h"
#include "../coco_internal.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

/* Platform-specific headers */
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

/* Forward declarations */
static bool adjust_frames_visitor(const coco_frame_info_t* frame, void* data);

/**
 * Check if stack growth is needed.
 */
bool coco_needs_stack_growth(
    uintptr_t stack_base,
    uintptr_t stack_limit,
    uintptr_t current_sp
) {
    // Stack grows downward: stack_base is lowest address, stack_limit is highest
    // Growth needed when SP is close to stack_base

    size_t stack_size = stack_limit - stack_base;
    size_t remaining = current_sp - stack_base;

    // Growth threshold: grow when less than 25% remaining
    size_t threshold = stack_size / 4;

    return remaining < threshold;
}

/**
 * Calculate new stack size.
 */
size_t coco_calc_new_stack_size(size_t current_size) {
    size_t new_size = current_size * COCO_STACK_GROW_FACTOR;

    // Cap at maximum size
    if (new_size > COCO_STACK_MAX_SIZE) {
        if (current_size >= COCO_STACK_MAX_SIZE) {
            return 0;  // Already at maximum
        }
        new_size = COCO_STACK_MAX_SIZE;
    }

    // Ensure page alignment
    size_t page_size = sysconf(_SC_PAGESIZE);
    new_size = ((new_size + page_size - 1) / page_size) * page_size;

    return new_size;
}

/**
 * Adjust frame pointers after stack copy.
 */
void coco_adjust_frame_pointers(
    uintptr_t old_base,
    uintptr_t old_size,
    uintptr_t new_base,
    uintptr_t* saved_fp,
    uintptr_t* saved_sp
) {
    // Calculate the delta: how much the stack moved
    // For downward-growing stack: delta = new_base - old_base

    uintptr_t delta = new_base - old_base;

    // Adjust saved FP: it was pointing into old stack, now should point to new stack
    if (*saved_fp >= old_base && *saved_fp < old_base + old_size) {
        *saved_fp += delta;
    }

    // Adjust saved SP similarly
    if (*saved_sp >= old_base && *saved_sp < old_base + old_size) {
        *saved_sp += delta;
    }
}

/**
 * Pointer adjustment callback for stack copying.
 */
typedef struct {
    uintptr_t old_base;
    uintptr_t old_limit;
    uintptr_t new_base;
    uintptr_t new_limit;
    uintptr_t delta;
    uint32_t adjusted_count;
} pointer_adjust_context_t;

static bool adjust_pointer_callback(
    uintptr_t ptr_addr,
    uintptr_t ptr_value,
    const coco_ptr_desc_t* desc,
    const coco_frame_info_t* frame_info,
    void* user_data
) {
    pointer_adjust_context_t* ctx = (pointer_adjust_context_t*)user_data;

    // Check if pointer points into old stack
    if (ptr_value >= ctx->old_base && ptr_value < ctx->old_limit) {
        // Adjust: add delta
        uintptr_t new_value = ptr_value + ctx->delta;

        // Write adjusted value back
        *(uintptr_t*)ptr_addr = new_value;
        ctx->adjusted_count++;
    }

    return true;  // Continue walking
}

/**
 * Adjust stack pointers after copy.
 */
uint32_t coco_adjust_stack_pointers(
    uintptr_t old_base,
    uintptr_t old_limit,
    uintptr_t new_base,
    uintptr_t new_limit,
    const coco_stack_map_t* stack_map,
    uintptr_t saved_fp,
    uintptr_t saved_sp
) {
    pointer_adjust_context_t ctx = {
        .old_base = old_base,
        .old_limit = old_limit,
        .new_base = new_base,
        .new_limit = new_limit,
        .delta = new_base - old_base,
        .adjusted_count = 0
    };

    // Walk frames and adjust pointers
    coco_walk_frames_with_visitor(
        stack_map,
        saved_fp,
        saved_sp,
        new_base,  // Use new stack bounds for validation
        new_limit,
        adjust_frames_visitor,
        &ctx
    );

    return ctx.adjusted_count;
}

// Frame visitor callback for adjusting pointers
static bool adjust_frames_visitor(
    const coco_frame_info_t* frame,
    void* data
) {
    pointer_adjust_context_t* ctx = (pointer_adjust_context_t*)data;

    if (frame->func_map) {
        coco_visit_frame_pointers(frame, NULL,
            adjust_pointer_callback, ctx);
    }

    return true;
}

/**
 * Grow a coroutine's stack.
 */
coco_grow_info_t coco_grow_stack(
    struct coco_ctx* ctx,
    const coco_stack_map_t* stack_map,
    uintptr_t current_sp,
    bool stack_from_pool,
    stack_pool_t* stack_pool
) {
    coco_grow_info_t info = {0};

    // Get current stack parameters from context
    info.old_base = (uintptr_t)ctx->stack_base;
    info.old_limit = (uintptr_t)ctx->stack_limit;

    if (info.old_base == 0 || info.old_limit == 0) {
        // No dynamic stack set up - use SP to estimate
        // This is a fallback for legacy coroutines
        info.old_limit = current_sp + 64 * 1024;  // Assume 64KB default
        info.old_base = current_sp;
    }

    info.old_size = info.old_limit - info.old_base;

    // Calculate new size
    size_t new_size = coco_calc_new_stack_size(info.old_size);
    if (new_size == 0) {
        info.result = COCO_GROW_ERROR_MAX;
        return info;
    }

    // Allocate new stack
    // Use mmap for page-aligned, guardable allocation
    size_t page_size = sysconf(_SC_PAGESIZE);
    void* new_stack = mmap(
        NULL,
        new_size + page_size,  // Extra page for guard
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );

    if (new_stack == MAP_FAILED) {
        info.result = COCO_GROW_ERROR_NOMEM;
        return info;
    }

    // Setup guard page at the bottom
    mprotect(new_stack, page_size, PROT_NONE);

    // Calculate new base (skip guard page)
    info.new_base = (uintptr_t)new_stack + page_size;
    info.new_size = new_size;
    info.new_limit = info.new_base + new_size;

    // Copy stack contents
    // Important: stack grows downward, so we copy from top
    // The portion above SP contains active data
    size_t copy_offset = current_sp - info.old_base;
    size_t copy_size = info.old_limit - current_sp;

    if (copy_size > 0 && copy_offset <= info.old_size) {
        // memmove handles overlapping regions
        memmove(
            (void*)(info.new_base + copy_offset),
            (void*)current_sp,
            copy_size
        );
    }

    // Get saved FP and SP from context
    uintptr_t saved_fp = (uintptr_t)ctx->fp;
    uintptr_t saved_sp = (uintptr_t)ctx->sp;

    // Adjust frame pointers
    coco_adjust_frame_pointers(
        info.old_base,
        info.old_size,
        info.new_base,
        &saved_fp,
        &saved_sp
    );

    // Adjust stack pointers
    if (stack_map) {
        info.pointers_adjusted = coco_adjust_stack_pointers(
            info.old_base,
            info.old_limit,
            info.new_base,
            info.new_limit,
            stack_map,
            saved_fp,
            saved_sp
        );
    }

    // Update context
    ctx->stack_base = (void*)info.new_base;
    ctx->stack_limit = (void*)info.new_limit;
    ctx->fp = (void*)saved_fp;
    ctx->sp = (void*)saved_sp;

    info.frames_adjusted = 0;  // Would need frame walk to count
    info.result = COCO_GROW_OK;

    // Free old stack
    // If old stack was from pool, return it to pool; otherwise munmap
    if (info.old_base != 0) {
        if (stack_from_pool && stack_pool) {
            // Return old stack to pool
            // old_limit is the stack top, old_size is the size
            stack_pool_free(stack_pool, (void*)info.old_limit, info.old_size);
        } else {
            // Old stack was directly mmap'd, munmap it
            size_t page_size = sysconf(_SC_PAGESIZE);
            void* old_stack_start = (void*)(info.old_base - page_size);  // Include guard page
            size_t old_total_size = info.old_size + page_size;
            munmap(old_stack_start, old_total_size);
        }
    }

    return info;
}