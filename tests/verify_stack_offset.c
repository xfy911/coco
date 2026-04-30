/**
 * verify_stack_offset.c - Runtime stack offset verification (ARM64 version)
 *
 * This test program measures actual stack offsets at runtime
 * and compares them with the offsets predicted by CocoStackPass.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// Test function with local variables
__attribute__((noinline))
void test_func(int *external_ptr) {
    int local1 = 1;
    int local2 = 2;
    int *ptr_to_local1 = &local1;    // Should be identified as stack pointer
    int *ptr_to_heap = (int*)malloc(sizeof(int)); // Should NOT be identified as stack pointer
    *ptr_to_heap = 42;

    // Measure actual stack offsets
    uintptr_t sp;
    uintptr_t fp;  // frame pointer (x29 on ARM64)

    // Get current SP and FP using inline assembly (ARM64)
    __asm__ volatile(
        "mov %0, sp\n"
        "mov %1, x29\n"
        : "=r"(sp), "=r"(fp)
    );

    // Calculate offsets relative to frame pointer
    uintptr_t local1_addr = (uintptr_t)&local1;
    uintptr_t local2_addr = (uintptr_t)&local2;
    uintptr_t ptr_to_local1_addr = (uintptr_t)&ptr_to_local1;
    uintptr_t ptr_to_heap_addr = (uintptr_t)&ptr_to_heap;

    int32_t local1_fp_offset = (int32_t)(local1_addr - fp);
    int32_t local2_fp_offset = (int32_t)(local2_addr - fp);
    int32_t ptr_to_local1_fp_offset = (int32_t)(ptr_to_local1_addr - fp);
    int32_t ptr_to_heap_fp_offset = (int32_t)(ptr_to_heap_addr - fp);

    printf("=== Runtime Stack Offset Measurement ===\n");
    printf("SP  = 0x%lx\n", sp);
    printf("FP  = 0x%lx (x29)\n", fp);
    printf("\n");
    printf("Local variables:\n");
    printf("  local1:        addr=0x%lx, fp_offset=%d\n", local1_addr, local1_fp_offset);
    printf("  local2:        addr=0x%lx, fp_offset=%d\n", local2_addr, local2_fp_offset);
    printf("  ptr_to_local1: addr=0x%lx, fp_offset=%d\n", ptr_to_local1_addr, ptr_to_local1_fp_offset);
    printf("  ptr_to_heap:   addr=0x%lx, fp_offset=%d\n", ptr_to_heap_addr, ptr_to_heap_fp_offset);
    printf("\n");
    printf("Pointer values:\n");
    printf("  ptr_to_local1 = 0x%lx (points to stack, should be adjusted)\n", (uintptr_t)ptr_to_local1);
    printf("  ptr_to_heap   = 0x%lx (points to heap, should NOT be adjusted)\n", (uintptr_t)ptr_to_heap);
    printf("\n");

    // Output to file for comparison with Pass predictions
    FILE *f = fopen("/tmp/coco_actual_offsets.txt", "w");
    if (f) {
        fprintf(f, "test_func\n");
        fprintf(f, "local1 %d 4\n", local1_fp_offset);
        fprintf(f, "local2 %d 4\n", local2_fp_offset);
        fprintf(f, "ptr_to_local1 %d 8\n", ptr_to_local1_fp_offset);
        fprintf(f, "ptr_to_heap %d 8\n", ptr_to_heap_fp_offset);
        fclose(f);
        printf("Offsets written to /tmp/coco_actual_offsets.txt\n");
    }

    // Clean up
    free(ptr_to_heap);
}

// Deep recursive test for frame chain validation
__attribute__((noinline))
void recursive_test(int depth, void *base_fp) {
    if (depth <= 0) {
        uintptr_t current_fp;
        __asm__ volatile("mov %0, x29" : "=r"(current_fp));

        printf("\n=== Frame Chain Validation ===\n");
        printf("Starting FP: 0x%lx\n", (uintptr_t)base_fp);
        printf("Current FP:  0x%lx\n", current_fp);
        printf("Depth reached: %d frames\n", 10 - depth);

        // Walk frame chain
        printf("Frame chain:\n");
        uintptr_t fp = current_fp;
        int frame_count = 0;
        while (fp && frame_count < 20) {
            uintptr_t prev_fp = *(uintptr_t*)fp;
            uintptr_t ret_addr = *(uintptr_t*)(fp + 8);
            printf("  Frame %d: fp=0x%lx, ret=0x%lx\n", frame_count, fp, ret_addr);

            if (prev_fp == 0) break;
            if (prev_fp > fp) {  // Frame pointers should decrease on ARM64
                printf("  WARNING: Frame chain broken at frame %d\n", frame_count);
                break;
            }
            fp = prev_fp;
            frame_count++;
        }
        printf("Total frames walked: %d\n", frame_count);
        return;
    }

    int local = depth;
    int *ptr = &local;

    recursive_test(depth - 1, base_fp);
}

int main(int argc, char **argv) {
    printf("Coco Stack Offset Verification (ARM64)\n");
    printf("======================================\n\n");

    uintptr_t initial_fp;
    __asm__ volatile("mov %0, x29" : "=r"(initial_fp));

    // Test 1: Basic stack offset measurement
    test_func(NULL);

    // Test 2: Deep recursion for frame chain validation
    printf("\n");
    recursive_test(10, (void*)initial_fp);

    printf("\n=== Tests Complete ===\n");
    return 0;
}