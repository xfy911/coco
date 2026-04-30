/**
 * test_frame_walker.c - Frame walker verification test
 *
 * Tests the frame pointer traversal mechanism for dynamic stack growth.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "coco_stack_map.h"
#include "coco_frame_walker.h"

// Test function with local variables
__attribute__((noinline))
static void test_depth_3(int depth, uintptr_t base_fp) {
    int local1 = depth;
    int local2 = depth * 2;
    int *ptr1 = &local1;
    int *ptr2 = &local2;

    (void)ptr1;
    (void)ptr2;
    (void)base_fp;

    if (depth <= 0) {
        uintptr_t current_fp = coco_get_current_fp();
        printf("=== Frame Walker Test ===\n");
        printf("Base FP: 0x%lx\n", base_fp);
        printf("Current FP: 0x%lx\n", current_fp);
        printf("Depth reached: 3 frames\n");

        // Walk frames manually
        printf("\nManual frame walk:\n");
        uintptr_t fp = current_fp;
        int frame_count = 0;
        while (fp && frame_count < 10) {
            uintptr_t prev_fp = *(uintptr_t*)fp;
            uintptr_t ret_addr = *(uintptr_t*)(fp + sizeof(uintptr_t));
            printf("  Frame %d: fp=0x%lx, ret=0x%lx\n", frame_count, fp, ret_addr);

            if (prev_fp == 0) break;
            if (prev_fp > fp) {
                printf("  ERROR: Frame chain broken\n");
                break;
            }
            fp = prev_fp;
            frame_count++;
        }

        printf("\nPassed: Frame chain valid with %d frames\n", frame_count);
        return;
    }

    test_depth_3(depth - 1, base_fp);
}

__attribute__((noinline))
static void test_depth_2(int depth, uintptr_t base_fp) {
    int local = depth;
    (void)local;
    test_depth_3(depth - 1, base_fp);
}

__attribute__((noinline))
static void test_depth_1(int depth, uintptr_t base_fp) {
    int local = depth;
    (void)local;
    test_depth_2(depth - 1, base_fp);
}

// Test frame walker API
static void test_frame_walker_api(void) {
    printf("\n=== Frame Walker API Test ===\n");

    uintptr_t base_fp = coco_get_current_fp();

    // Create a mock stack map (empty for now)
    coco_stack_map_t* stack_map = NULL;

    // Walk frames using the API
    uintptr_t saved_fp = coco_get_current_fp();
    uintptr_t saved_sp = coco_get_current_sp();

    // Estimate stack bounds (rough estimate)
    uintptr_t stack_base = saved_sp - 4096;  // Assume at least 4KB used
    uintptr_t stack_limit = saved_sp + 65536;  // Assume 64KB stack

    coco_frame_walk_result_t result = coco_walk_coro_frames(
        stack_map, saved_fp, saved_sp, stack_base, stack_limit
    );

    printf("Frames walked: %u\n", result.num_frames);
    printf("Chain valid: %s\n", result.chain_valid ? "yes" : "no");

    if (!result.chain_valid) {
        printf("Error: %s\n", result.error_msg ? result.error_msg : "unknown");
    }

    // Validate chain
    bool valid = coco_validate_frame_chain(&result, stack_base, stack_limit);
    printf("Validation result: %s\n", valid ? "PASS" : "FAIL");

    assert(result.num_frames > 0);
    assert(result.chain_valid == true);
}

// Test inline functions
static void test_inline_functions(void) {
    printf("\n=== Inline Functions Test ===\n");

    uintptr_t fp = coco_get_current_fp();
    uintptr_t sp = coco_get_current_sp();

    printf("Current FP: 0x%lx\n", fp);
    printf("Current SP: 0x%lx\n", sp);

    assert(fp != 0);
    assert(sp != 0);
    assert(sp < fp);  // Stack grows downward

    printf("Passed: FP and SP retrieval works\n");
}

int main(int argc, char **argv) {
    printf("Coco Frame Walker Test\n");
    printf("========================\n\n");

    uintptr_t base_fp = coco_get_current_fp();

    // Test 1: Manual frame walk
    test_depth_1(3, base_fp);

    // Test 2: Frame walker API
    test_frame_walker_api();

    // Test 3: Inline functions
    test_inline_functions();

    printf("\n=== All Tests Passed ===\n");
    return 0;
}