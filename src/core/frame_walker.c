/**
 * frame_walker.c - 帧指针遍历实现
 *
 * 实现动态栈增长所需的栈帧遍历。
 *
 * @file frame_walker.c
 * @brief 帧指针遍历运行时实现
 */

#include "coco_frame_walker.h"
#include "coco_stack_map.h"
#include <string.h>

/**
 * 从已挂起协程遍历栈帧链。
 *
 * 在 x86-64 和 ARM64 上，帧指针保存在每个栈帧开头，形成链表：
 * - FP 指向前一个保存的 FP，位于 FP[0]
 * - 返回地址保存在 FP[8]（或 ARM64 的 FP+8）
 * - 帧指针向上（栈向下增长）递减
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

        // Validate chain: On ARM64, FP increases as we go up (stack grows downward)
        // On x86-64, FP decreases as we go up
        // For ARM64: prev_fp should be >= current fp (or 0 for base)
#if defined(__aarch64__)
        if (prev_fp != 0 && prev_fp < fp) {
            // ARM64: prev_fp should be higher (closer to stack base)
            if (prev_fp < fp - 1024) {
                result.chain_valid = false;
                result.error_msg = "Frame chain broken: FP decreased unexpectedly on ARM64";
                break;
            }
        }
#else
        // x86-64: FP should decrease as we go up the stack
        if (prev_fp != 0 && prev_fp >= fp) {
            if (prev_fp > fp + 1024) {
                result.chain_valid = false;
                result.error_msg = "Frame chain broken: FP did not decrease";
                break;
            }
        }
#endif

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
 * 使用回调访问者遍历栈帧。
 */
int coco_walk_frames_with_visitor((
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
 * 使用栈映射信息访问帧中的所有指针。
 */
int coco_visit_frame_pointers((
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
 * 验证栈帧链完整性。
 */
bool coco_validate_frame_chain((
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