/**
 * coco_stack_grow.h - Dynamic stack growth runtime API
 *
 * @file coco_stack_grow.h
 * @brief Stack growth mechanism for coroutines
 */

#ifndef COCO_STACK_GROW_H
#define COCO_STACK_GROW_H

#include "coco_stack_map.h"
#include "coco_frame_walker.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>

/* Forward declaration */
struct coco_ctx;
struct stack_pool;

#ifdef __cplusplus
extern "C" {
#endif

/* === Stack Growth Constants === */
/** Stack growth factor (new_size = current_size * factor) */
#define COCO_STACK_GROW_FACTOR 2

/** Maximum growth attempts before giving up */
#define COCO_STACK_MAX_GROW_ATTEMPTS 3

/* === Stack Growth Result === */

/**
 * @brief Result of a stack growth attempt
 */
typedef enum coco_grow_result {
    COCO_GROW_OK = 0,           /**< Growth successful */
    COCO_GROW_ERROR_NOMEM = -1, /**< Memory allocation failed */
    COCO_GROW_ERROR_MAX = -2,   /**< Reached maximum stack size */
    COCO_GROW_ERROR_CHAIN = -3, /**< Frame chain invalid */
    COCO_GROW_ERROR_BOUNDS = -4 /**< Pointer outside stack bounds */
} coco_grow_result_t;

/**
 * @brief Information about a stack growth operation
 */
typedef struct coco_grow_info {
    uintptr_t old_base;     /**< Old stack base address */
    uintptr_t old_limit;    /**< Old stack limit address */
    size_t old_size;        /**< Old stack size */

    uintptr_t new_base;     /**< New stack base address */
    uintptr_t new_limit;    /**< New stack limit address */
    size_t new_size;        /**< New stack size */

    uint32_t frames_adjusted; /**< Number of frames adjusted */
    uint32_t pointers_adjusted; /**< Number of pointers adjusted */

    coco_grow_result_t result; /**< Growth result */
} coco_grow_info_t;

/* === API Functions === */

/**
 * @brief Check if stack growth is needed
 *
 * @param stack_base Current stack base
 * @param stack_limit Current stack limit
 * @param current_sp Current stack pointer position
 * @return true if growth is needed, false otherwise
 */
bool coco_needs_stack_growth(
    uintptr_t stack_base,
    uintptr_t stack_limit,
    uintptr_t current_sp
);

/**
 * @brief Calculate new stack size
 *
 * Uses the growth factor to double the current size,
 * capped at COCO_STACK_MAX_SIZE.
 *
 * @param current_size Current stack size
 * @return New stack size, or 0 if already at maximum
 */
size_t coco_calc_new_stack_size(size_t current_size);

/**
 * @brief Grow a coroutine's stack
 *
 * This is the main entry point for stack growth:
 * 1. Allocate new stack
 * 2. Copy stack contents
 * 3. Adjust frame pointers
 * 4. Adjust stack pointers
 * 5. Update coroutine context
 *
 * @param ctx The coroutine context to grow
 * @param stack_map The loaded stack map for pointer identification
 * @param current_sp Current stack pointer (from saved context or signal)
 * @param stack_from_pool Whether the old stack was allocated from pool
 * @param stack_pool The stack pool to return old stack to (if from pool), or NULL
 * @return Growth information structure
 */
coco_grow_info_t coco_grow_stack(
    struct coco_ctx* ctx,
    const coco_stack_map_t* stack_map,
    uintptr_t current_sp,
    bool stack_from_pool,
    struct stack_pool* stack_pool
);

/**
 * @brief Adjust frame pointers after stack copy
 *
 * Frame pointers need to be updated to point to the new stack location.
 * The adjustment delta is: new_base - old_base
 *
 * @param old_base Old stack base address
 * @param old_size Old stack size
 * @param new_base New stack base address
 * @param saved_fp Saved frame pointer (will be updated)
 * @param saved_sp Saved stack pointer (will be updated)
 */
void coco_adjust_frame_pointers(
    uintptr_t old_base,
    uintptr_t old_size,
    uintptr_t new_base,
    uintptr_t* saved_fp,
    uintptr_t* saved_sp
);

/**
 * @brief Adjust stack pointers after copy
 *
 * Walks the frame chain and adjusts each pointer that points
 * to the old stack to point to the corresponding location
 * in the new stack.
 *
 * @param old_base Old stack base address
 * @param old_limit Old stack limit address
 * @param new_base New stack base address
 * @param new_limit New stack limit address
 * @param stack_map The loaded stack map
 * @param saved_fp Starting frame pointer
 * @param saved_sp Starting stack pointer
 * @return Number of pointers adjusted
 */
uint32_t coco_adjust_stack_pointers(
    uintptr_t old_base,
    uintptr_t old_limit,
    uintptr_t new_base,
    uintptr_t new_limit,
    const coco_stack_map_t* stack_map,
    uintptr_t saved_fp,
    uintptr_t saved_sp
);

#ifdef __cplusplus
}
#endif

#endif /* COCO_STACK_GROW_H */