/**
 * coco_frame_walker.h - Frame pointer traversal for stack growth
 *
 * This header defines the API for walking coroutine stack frames
 * and identifying pointers that need adjustment during stack copying.
 *
 * @file coco_frame_walker.h
 * @brief Frame pointer traversal API
 */

#ifndef COCO_FRAME_WALKER_H
#define COCO_FRAME_WALKER_H

#include "coco_stack_map.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Frame Walker Constants === */
/** @defgroup FrameWalkerConstants Frame Walker Constants
 *  @{
 */

/** Maximum number of frames to walk (prevent infinite loops) */
#define COCO_MAX_FRAMES 256

/** Frame chain validation flags */
#define COCO_FRAME_VALID      0x01
#define COCO_FRAME_SUSPENDED  0x02
#define COCO_FRAME_CORO       0x04

/** @} */

/* === Frame Info Structure === */

/**
 * @struct coco_frame_info_t
 * @brief Information about a single frame during traversal
 */
typedef struct coco_frame_info {
    /** Frame pointer address (FP/x29/rbp) */
    uintptr_t frame_ptr;

    /** Return address (saved instruction pointer) */
    uintptr_t return_addr;

    /** Stack base for this frame */
    uintptr_t stack_base;

    /** Stack limit for this frame */
    uintptr_t stack_limit;

    /** Function map entry (may be NULL if not found) */
    const coco_func_map_t* func_map;

    /** Frame index (0 = innermost/called last) */
    uint32_t frame_index;

    /** Validation flags */
    uint32_t flags;
} coco_frame_info_t;

/**
 * @struct coco_frame_walk_result_t
 * @brief Result of a complete frame chain walk
 */
typedef struct coco_frame_walk_result {
    /** Number of frames successfully walked */
    uint32_t num_frames;

    /** Frame info array */
    coco_frame_info_t frames[COCO_MAX_FRAMES];

    /** Whether the chain is valid (FP decreases monotonically) */
    bool chain_valid;

    /** Error message if chain is broken */
    const char* error_msg;
} coco_frame_walk_result_t;

/* === Callback Types === */

/**
 * @brief Callback function for each frame visited
 *
 * @param frame_info Information about the current frame
 * @param user_data User-provided context
 * @return true to continue walking, false to stop
 */
typedef bool (*coco_frame_visitor_fn)(
    const coco_frame_info_t* frame_info,
    void* user_data
);

/**
 * @brief Callback for each pointer found in a frame
 *
 * @param ptr_addr Address of the pointer on the stack
 * @param ptr_value Value stored in the pointer
 * @param desc Pointer descriptor from stack map
 * @param frame_info The frame containing this pointer
 * @param user_data User-provided context
 * @return true to continue, false to stop
 */
typedef bool (*coco_pointer_visitor_fn)(
    uintptr_t ptr_addr,
    uintptr_t ptr_value,
    const coco_ptr_desc_t* desc,
    const coco_frame_info_t* frame_info,
    void* user_data
);

/* === API Functions === */

/**
 * @brief Walk the frame chain of a suspended coroutine
 *
 * Starts from the saved FP in the coroutine context and walks
 * up through parent frames until reaching the main stack.
 *
 * @param stack_map The loaded stack map for function lookup
 * @param saved_fp The saved frame pointer from coroutine context
 * @param saved_sp The saved stack pointer from coroutine context
 * @param stack_base Base address of coroutine stack
 * @param stack_limit Current limit of coroutine stack
 * @return Result structure with frame chain information
 */
coco_frame_walk_result_t coco_walk_coro_frames(
    const coco_stack_map_t* stack_map,
    uintptr_t saved_fp,
    uintptr_t saved_sp,
    uintptr_t stack_base,
    uintptr_t stack_limit
);

/**
 * @brief Walk frames with a callback visitor
 *
 * More flexible version that calls a callback for each frame.
 *
 * @param stack_map The loaded stack map
 * @param saved_fp Starting frame pointer
 * @param saved_sp Starting stack pointer
 * @param stack_base Stack base address
 * @param stack_limit Stack limit address
 * @param visitor Callback function for each frame
 * @param user_data User context passed to callback
 * @return Number of frames visited, or -1 on error
 */
int coco_walk_frames_with_visitor(
    const coco_stack_map_t* stack_map,
    uintptr_t saved_fp,
    uintptr_t saved_sp,
    uintptr_t stack_base,
    uintptr_t stack_limit,
    coco_frame_visitor_fn visitor,
    void* user_data
);

/**
 * @brief Visit all pointers in a frame
 *
 * Uses the stack map to find all pointer locations in a frame
 * and calls the callback for each one.
 *
 * @param frame_info The frame to examine
 * @param stack_map The loaded stack map
 * @param visitor Callback for each pointer found
 * @param user_data User context
 * @return Number of pointers visited
 */
int coco_visit_frame_pointers(
    const coco_frame_info_t* frame_info,
    const coco_stack_map_t* stack_map,
    coco_pointer_visitor_fn visitor,
    void* user_data
);

/**
 * @brief Validate frame chain integrity
 *
 * Checks that frame pointers decrease monotonically and
 * all frames are within the stack bounds.
 *
 * @param result The frame walk result to validate
 * @param stack_base Expected stack base
 * @param stack_limit Expected stack limit
 * @return true if chain is valid, false otherwise
 */
bool coco_validate_frame_chain(
    const coco_frame_walk_result_t* result,
    uintptr_t stack_base,
    uintptr_t stack_limit
);

/**
 * @brief Get current frame pointer (arch-specific)
 *
 * Inline function to get the current FP value.
 * Architecture-dependent implementation.
 */
static inline uintptr_t coco_get_current_fp(void) {
#if defined(__x86_64__)
    uintptr_t fp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(fp));
    return fp;
#elif defined(__aarch64__)
    uintptr_t fp;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    return fp;
#else
    return 0;  /* Unsupported architecture */
#endif
}

/**
 * @brief Get current stack pointer (arch-specific)
 */
static inline uintptr_t coco_get_current_sp(void) {
#if defined(__x86_64__)
    uintptr_t sp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    return sp;
#elif defined(__aarch64__)
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
#else
    return 0;  /* Unsupported architecture */
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* COCO_FRAME_WALKER_H */