/**
 * coco_safety.h - Safety levels for dynamic stack growth
 *
 * @file coco_safety.h
 * @brief Safety mode definitions and API
 */

#ifndef COCO_SAFETY_H
#define COCO_SAFETY_H

#include "coco_stack_grow.h"
#include "coco_stack_map.h"
#include "coco_frame_walker.h"
#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct coco_coro;
struct coco_sched;
typedef struct coco_coro coco_coro_t;
typedef struct coco_sched coco_sched_t;

#ifdef __cplusplus
extern "C" {
#endif

/* === Safety Levels === */

/**
 * @brief Safety modes for dynamic stack growth
 *
 * Different safety levels balance between:
 * - Performance: faster stack operations
 * - Safety: more thorough pointer adjustment
 * - Memory: stack growth/shrink behavior
 */
typedef enum coco_safety_mode {
    /**
     * Level 0: No dynamic stack
     * - Fixed 64KB stack (current coco default)
     * - Stack overflow causes fatal error
     * - Maximum performance, no safety overhead
     */
    COCO_SAFETY_NONE = 0,

    /**
     * Level 1: Conservative growth
     * - Grow on overflow, never shrink
     * - Adjust only known pointers (from stack map)
     * - Medium performance overhead
     * - Safe for most applications
     */
    COCO_SAFETY_CONSERVATIVE = 1,

    /**
     * Level 2: Full dynamic stack
     * - Grow on overflow, shrink when possible
     * - Adjust all potential pointers (conservative scan)
     * - Scan entire stack for pointer candidates
     * - Higher overhead, maximum safety
     * - Recommended for unknown code patterns
     */
    COCO_SAFETY_FULL = 2
} coco_safety_mode_t;

/* === Safety Configuration === */

/**
 * @brief Configuration for a safety mode
 */
typedef struct coco_safety_config {
    /** Current safety mode */
    coco_safety_mode_t mode;

    /** Minimum stack size for this mode */
    size_t min_stack_size;

    /** Maximum stack size for this mode */
    size_t max_stack_size;

    /** Growth threshold (percentage, 0-100) */
    uint8_t grow_threshold_percent;

    /** Shrink threshold (percentage, 0-100) */
    uint8_t shrink_threshold_percent;

    /** Enable pointer scanning (Level 2) */
    bool scan_all_pointers;

    /** Enable automatic shrinking */
    bool auto_shrink;
} coco_safety_config_t;

/* === API Functions === */

/**
 * @brief Get default configuration for a safety mode
 *
 * @param mode The safety mode
 * @return Default configuration for that mode
 */
coco_safety_config_t coco_get_default_config(coco_safety_mode_t mode);

/**
 * @brief Set global safety mode
 *
 * Affects all newly created coroutines.
 *
 * @param mode The safety mode to set
 * @return Previous safety mode
 */
coco_safety_mode_t coco_set_safety_mode(coco_safety_mode_t mode);

/**
 * @brief Get current global safety mode
 *
 * @return Current global safety mode
 */
coco_safety_mode_t coco_get_safety_mode(void);

/**
 * @brief Create a coroutine with specific safety mode
 *
 * @param sched The scheduler to create the coroutine in
 * @param entry Entry function
 * @param arg Argument to pass
 * @param stack_size Initial stack size (0 = default for mode)
 * @param mode Safety mode (use global mode if COCO_SAFETY_NONE)
 * @return Created coroutine handle, or NULL on error
 */
coco_coro_t* coco_create_safe(
    coco_sched_t* sched,
    void (*entry)(void*),
    void* arg,
    size_t stack_size,
    coco_safety_mode_t mode
);

/**
 * @brief Check if a coroutine can shrink its stack
 *
 * @param coro The coroutine to check
 * @param config The safety configuration
 * @return true if shrinking is possible and beneficial
 */
bool coco_can_shrink_stack(
    const coco_coro_t* coro,
    const coco_safety_config_t* config
);

/**
 * @brief Shrink a coroutine's stack
 *
 * @param coro The coroutine to shrink
 * @param stack_map The loaded stack map
 * @param config The safety configuration
 * @return true if shrinking succeeded
 */
bool coco_shrink_stack(
    coco_coro_t* coro,
    const coco_stack_map_t* stack_map,
    const coco_safety_config_t* config
);

/**
 * @brief Scan stack for potential pointers (Level 2 safety)
 *
 * Conservative scanning: treat all 8-byte aligned values
 * within stack bounds as potential pointers.
 *
 * @param stack_base Stack base address
 * @param stack_limit Stack limit address
 * @param current_sp Current stack pointer
 * @param visitor Callback for each potential pointer
 * @param user_data User context for callback
 * @return Number of potential pointers found
 */
uint32_t coco_scan_stack_pointers(
    uintptr_t stack_base,
    uintptr_t stack_limit,
    uintptr_t current_sp,
    coco_pointer_visitor_fn visitor,
    void* user_data
);

#ifdef __cplusplus
}
#endif

#endif /* COCO_SAFETY_H */