/**
 * frame_walker.c - Frame pointer traversal implementation
 *
 * Implements stack frame walking for dynamic stack growth.
 *
 * @file frame_walker.c
 * @brief Frame walker runtime implementation
 */

#include "coco_frame_walker.h"
#include "coco_stack_map.h"
#include <string.h>

/**
 * Walk the frame chain from a suspended coroutine.
 *
 * On x86-64 and ARM64, frame pointers are saved at the beginning
 * of each stack frame, forming a linked list:
 * - FP points to saved previous FP at FP[0]
 * - Return address is saved at FP[8] (or FP+8 on ARM64)
 * - Frame pointers decrease as we go up the stack (stack grows down)
 */
coco_frame_walk_result_t coco_walk_coro_frames(
    const coco_stack_map_t* stack_map,
    uintptr_t saved_fp,
    uintptr_t saved_sp,
    uintptr_t stack_base,
    uintptr_t stack_limit
) {
    coco_frame_walk_result_t result;
    memset(&result, 0, sizeof(result));
    result.chain_valid = true;

    uintptr_t fp = saved_fp;
    uintptr_t prev_fp = 0;
    uint32_t frame_idx = 0;

    while (fp != 0 && frame_idx < COCO_MAX_FRAMES) {
        coco_frame_info_t* frame = &result.frames[frame_idx];
        frame->frame_ptr = fp;
        frame->frame_index = frame_idx;

        // Check bounds: FP should be within stack range
        if (!coco_addr_in_stack(stack_base, stack_limit, fp)) {
            result.chain_valid = false;
            result.error_msg = "Frame pointer outside stack bounds";
            break;
        }

        // Read saved previous FP and return address
        // Frame layout: [prev_fp][ret_addr][local_vars...]
        prev_fp = *(uintptr_t*)fp;
        uintptr_t ret_addr = *(uintptr_t*)(fp + sizeof(uintptr_t));

        frame->return_addr = ret_addr;

        // Look up function map for this address
        if (stack_map) {
            frame->func_map = coco_find_func_map(stack_map, ret_addr);
        }

        // Validate chain: FP should decrease (stack grows downward)
        if (prev_fp != 0 && prev_fp >= fp) {
            // On some platforms, the outermost frame may have prev_fp > fp
            // But for coroutine frames, we expect monotonically decreasing
            if (prev_fp > fp + 1024) {  // Allow small upward jumps for leaf frames
                result.chain_valid = false;
                result.error_msg = "Frame chain broken: FP did not decrease";
                break;
            }
        }

        // Set validation flags
        frame->flags = COCO_FRAME_VALID;

        // Move to next frame
        fp = prev_fp;
        frame_idx++;
    }

    result.num_frames = frame_idx;
    return result;
}

/**
 * Walk frames with callback visitor.
 */
int coco_walk_frames_with_visitor(
    const coco_stack_map_t* stack_map,
    uintptr_t saved_fp,
    uintptr_t saved_sp,
    uintptr_t stack_base,
    uintptr_t stack_limit,
    coco_frame_visitor_fn visitor,
    void* user_data
) {
    uintptr_t fp = saved_fp;
    int frame_count = 0;

    while (fp != 0 && frame_count < COCO_MAX_FRAMES) {
        // Check bounds
        if (!coco_addr_in_stack(stack_base, stack_limit, fp)) {
            return -1;
        }

        // Build frame info
        coco_frame_info_t frame_info;
        memset(&frame_info, 0, sizeof(frame_info));
        frame_info.frame_ptr = fp;
        frame_info.frame_index = frame_count;
        frame_info.return_addr = *(uintptr_t*)(fp + sizeof(uintptr_t));
        frame_info.flags = COCO_FRAME_VALID;

        if (stack_map) {
            frame_info.func_map = coco_find_func_map(stack_map, frame_info.return_addr);
        }

        // Call visitor
        if (!visitor(&frame_info, user_data)) {
            break;
        }

        // Move to next frame
        uintptr_t prev_fp = *(uintptr_t*)fp;
        if (prev_fp != 0 && prev_fp >= fp && prev_fp > fp + 1024) {
            break;  // Chain broken
        }
        fp = prev_fp;
        frame_count++;
    }

    return frame_count;
}

/**
 * Visit all pointers in a frame using stack map info.
 */
int coco_visit_frame_pointers(
    const coco_frame_info_t* frame_info,
    const coco_stack_map_t* stack_map,
    coco_pointer_visitor_fn visitor,
    void* user_data
) {
    if (!frame_info || !frame_info->func_map || !visitor) {
        return 0;
    }

    const coco_func_map_t* map = frame_info->func_map;
    int count = 0;

    for (uint32_t i = 0; i < map->num_pointers; i++) {
        const coco_ptr_desc_t* desc = &map->pointers[i];

        // Calculate absolute address from FP-relative offset
        uintptr_t ptr_addr = frame_info->frame_ptr + desc->frame_offset;

        // Read the pointer value
        uintptr_t ptr_value = 0;
        if (desc->size == 8) {
            ptr_value = *(uintptr_t*)ptr_addr;
        } else if (desc->size == 4) {
            ptr_value = *(uint32_t*)ptr_addr;
        }

        // Call visitor
        if (!visitor(ptr_addr, ptr_value, desc, frame_info, user_data)) {
            break;
        }
        count++;
    }

    return count;
}

/**
 * Validate frame chain integrity.
 */
bool coco_validate_frame_chain(
    const coco_frame_walk_result_t* result,
    uintptr_t stack_base,
    uintptr_t stack_limit
) {
    if (!result || result->num_frames == 0) {
        return false;
    }

    // Check overall validity flag
    if (!result->chain_valid) {
        return false;
    }

    // Verify all frames are within bounds
    for (uint32_t i = 0; i < result->num_frames; i++) {
        const coco_frame_info_t* frame = &result->frames[i];

        if (!coco_addr_in_stack(stack_base, stack_limit, frame->frame_ptr)) {
            return false;
        }

        // Check FP monotonicity (should decrease as we go up)
        if (i > 0) {
            uintptr_t prev_fp = result->frames[i - 1].frame_ptr;
            uintptr_t curr_fp = frame->frame_ptr;

            // Allow small upward jumps for leaf frames
            if (curr_fp > prev_fp && curr_fp > prev_fp + 1024) {
                return false;
            }
        }
    }

    return true;
}