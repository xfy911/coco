/**
 * test_stack_map.c - Stack map loading and binary search tests
 *
 * Tests stack map file I/O, binary search lookup, and memory management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "coco_stack_map.h"

#define STACK_MAP_MAGIC 0xC0C0

/* ===== Helper: Create a test stack map file ===== */

static void create_test_stack_map_file(const char *path, uint32_t num_funcs) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);

    /* Write header */
    uint32_t header[3] = { STACK_MAP_MAGIC, 1, num_funcs };
    fwrite(header, sizeof(header), 1, f);

    /* Write function entries */
    for (uint32_t i = 0; i < num_funcs; i++) {
        uint64_t func_addr = 0x400000 + (i * 0x1000);
        uint64_t func_size = 0x100;
        uint32_t frame_size = 256;
        uint32_t num_pointers = 2 + i;  /* Varying pointer counts */

        /* Write function metadata */
        fwrite(&func_addr, sizeof(uint64_t), 1, f);
        fwrite(&func_size, sizeof(uint64_t), 1, f);
        fwrite(&frame_size, sizeof(uint32_t), 1, f);
        fwrite(&num_pointers, sizeof(uint32_t), 1, f);

        /* Write pointer descriptors */
        for (uint32_t j = 0; j < num_pointers; j++) {
            int32_t frame_offset = -8 * (j + 1);
            uint16_t size = 8;
            uint8_t kind = COCO_PTR_LOCAL_PTR;
            uint8_t flags = 0;

            fwrite(&frame_offset, sizeof(int32_t), 1, f);
            fwrite(&size, sizeof(uint16_t), 1, f);
            fwrite(&kind, sizeof(uint8_t), 1, f);
            fwrite(&flags, sizeof(uint8_t), 1, f);
        }
    }

    fclose(f);
}

static void create_invalid_stack_map_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

/* ===== Test: Stack Map Loading ===== */

static void test_load_valid_stack_map(void) {
    printf("Test: Load valid stack map from file\n");

    const char *test_path = "/tmp/test_stack_map.coco";

    /* Create a test file with 3 functions */
    create_test_stack_map_file(test_path, 3);

    /* Load the stack map */
    coco_stack_map_t *map = coco_load_stack_map(test_path);
    assert(map != NULL);
    assert(map->magic == STACK_MAP_MAGIC);
    assert(map->version == 1);
    assert(map->num_funcs == 3);

    coco_free_stack_map(map);

    /* Cleanup */
    remove(test_path);

    printf("  PASSED: Stack map loading works\n");
}

static void test_load_invalid_stack_map(void) {
    printf("Test: Load invalid stack map files\n");

    const char *test_path = "/tmp/test_stack_map_invalid.coco";

    /* Test with non-existent file */
    coco_stack_map_t *map = coco_load_stack_map("/tmp/does_not_exist.coco");
    assert(map == NULL);

    /* Test with invalid magic number */
    create_invalid_stack_map_file(test_path, "NOT_A_STACK_MAP_FILE");
    map = coco_load_stack_map(test_path);
    assert(map == NULL);

    /* Test with truncated file */
    create_invalid_stack_map_file(test_path, "\xC0\xC0");
    map = coco_load_stack_map(test_path);
    assert(map == NULL);

    /* Cleanup */
    remove(test_path);

    printf("  PASSED: Invalid stack map loading handled correctly\n");
}

static void test_free_null_stack_map(void) {
    printf("Test: Free NULL stack map\n");

    /* Should not crash */
    coco_free_stack_map(NULL);

    printf("  PASSED: Free NULL handled gracefully\n");
}

/* ===== Test: Binary Search Lookup ===== */

static void create_test_stack_map_for_search(const char *path) {
    /* Create 5 functions with known addresses */
    FILE *f = fopen(path, "wb");
    assert(f != NULL);

    uint32_t header[3] = { STACK_MAP_MAGIC, 1, 5 };
    fwrite(header, sizeof(header), 1, f);

    /* Functions at: 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, each 0x100 bytes */
    uint64_t addrs[] = { 0x1000, 0x2000, 0x3000, 0x4000, 0x5000 };

    for (int i = 0; i < 5; i++) {
        uint64_t func_addr = addrs[i];
        uint64_t func_size = 0x100;
        uint32_t frame_size = 128;
        uint32_t num_pointers = 1;

        fwrite(&func_addr, sizeof(uint64_t), 1, f);
        fwrite(&func_size, sizeof(uint64_t), 1, f);
        fwrite(&frame_size, sizeof(uint32_t), 1, f);
        fwrite(&num_pointers, sizeof(uint32_t), 1, f);

        /* Single pointer descriptor */
        int32_t frame_offset = -8;
        uint16_t size = 8;
        uint8_t kind = COCO_PTR_LOCAL_PTR;
        uint8_t flags = 0;

        fwrite(&frame_offset, sizeof(int32_t), 1, f);
        fwrite(&size, sizeof(uint16_t), 1, f);
        fwrite(&kind, sizeof(uint8_t), 1, f);
        fwrite(&flags, sizeof(uint8_t), 1, f);
    }

    fclose(f);
}

static void test_find_func_map(void) {
    printf("Test: Binary search for function map\n");

    const char *test_path = "/tmp/test_stack_map_search.coco";
    create_test_stack_map_for_search(test_path);

    coco_stack_map_t *map = coco_load_stack_map(test_path);
    assert(map != NULL);

    /* Test finding function at start */
    coco_func_map_t *entry = coco_find_func_map(map, 0x1050);
    assert(entry != NULL);
    assert(entry->func_addr == 0x1000);
    assert(entry->func_size == 0x100);

    /* Test finding function in middle */
    entry = coco_find_func_map(map, 0x3080);
    assert(entry != NULL);
    assert(entry->func_addr == 0x3000);

    /* Test finding function at end */
    entry = coco_find_func_map(map, 0x50FF);
    assert(entry != NULL);
    assert(entry->func_addr == 0x5000);

    /* Test address before all functions */
    entry = coco_find_func_map(map, 0x0500);
    assert(entry == NULL);

    /* Test address after all functions */
    entry = coco_find_func_map(map, 0x6000);
    assert(entry == NULL);

    /* Test address between functions */
    entry = coco_find_func_map(map, 0x1200);
    assert(entry == NULL);

    /* Test NULL map */
    entry = coco_find_func_map(NULL, 0x1000);
    assert(entry == NULL);

    /* Test with invalid magic */
    map->magic = 0xDEAD;
    entry = coco_find_func_map(map, 0x1000);
    assert(entry == NULL);
    map->magic = STACK_MAP_MAGIC;

    coco_free_stack_map(map);
    remove(test_path);

    printf("  PASSED: Binary search lookup works correctly\n");
}

/* ===== Test: Address in Stack Check ===== */

static void test_addr_in_stack(void) {
    printf("Test: Address in stack check\n");

    uintptr_t stack_base = 0x100000000;
    uintptr_t stack_limit = 0x100010000;  /* 64KB */

    /* Inside stack */
    assert(coco_addr_in_stack(stack_base, stack_limit, 0x100005000) == true);
    assert(coco_addr_in_stack(stack_base, stack_limit, stack_base) == true);
    assert(coco_addr_in_stack(stack_base, stack_limit, stack_limit - 1) == true);

    /* Outside stack */
    assert(coco_addr_in_stack(stack_base, stack_limit, stack_base - 1) == false);
    assert(coco_addr_in_stack(stack_base, stack_limit, stack_limit) == false);
    assert(coco_addr_in_stack(stack_base, stack_limit, 0x0) == false);
    assert(coco_addr_in_stack(stack_base, stack_limit, 0xFFFFFFFFFFFFFFFF) == false);

    printf("  PASSED: Address in stack check works\n");
}

/* ===== Test: Multiple Pointer Descriptors ===== */

static void test_multiple_pointers(void) {
    printf("Test: Stack map with multiple pointer descriptors\n");

    const char *test_path = "/tmp/test_stack_map_multi.coco";

    /* Create a function with many pointers */
    FILE *f = fopen(test_path, "wb");
    assert(f != NULL);

    uint32_t header[3] = { STACK_MAP_MAGIC, 1, 1 };
    fwrite(header, sizeof(header), 1, f);

    uint64_t func_addr = 0x1000;
    uint64_t func_size = 0x200;
    uint32_t frame_size = 512;
    uint32_t num_pointers = 10;

    fwrite(&func_addr, sizeof(uint64_t), 1, f);
    fwrite(&func_size, sizeof(uint64_t), 1, f);
    fwrite(&frame_size, sizeof(uint32_t), 1, f);
    fwrite(&num_pointers, sizeof(uint32_t), 1, f);

    /* 10 different pointer types */
    uint8_t kinds[] = {
        COCO_PTR_FRAME_PTR, COCO_PTR_RETURN_ADDR, COCO_PTR_LOCAL_PTR,
        COCO_PTR_SPILL_REG, COCO_PTR_MAYBE_PTR, COCO_PTR_UNKNOWN,
        COCO_PTR_LOCAL_PTR, COCO_PTR_LOCAL_PTR, COCO_PTR_LOCAL_PTR,
        COCO_PTR_LOCAL_PTR
    };

    for (uint32_t i = 0; i < num_pointers; i++) {
        int32_t frame_offset = -8 * (i + 1);
        uint16_t size = 8;
        uint8_t kind = kinds[i];
        uint8_t flags = 0;

        fwrite(&frame_offset, sizeof(int32_t), 1, f);
        fwrite(&size, sizeof(uint16_t), 1, f);
        fwrite(&kind, sizeof(uint8_t), 1, f);
        fwrite(&flags, sizeof(uint8_t), 1, f);
    }

    fclose(f);

    /* Load and verify */
    coco_stack_map_t *map = coco_load_stack_map(test_path);
    assert(map != NULL);
    assert(map->num_funcs == 1);

    /* Find the function and check pointers */
    coco_func_map_t *entry = coco_find_func_map(map, 0x1080);
    assert(entry != NULL);
    assert(entry->num_pointers == 10);
    assert(entry->pointers[0].kind == COCO_PTR_FRAME_PTR);
    assert(entry->pointers[1].kind == COCO_PTR_RETURN_ADDR);
    assert(entry->pointers[3].kind == COCO_PTR_SPILL_REG);

    coco_free_stack_map(map);
    remove(test_path);

    printf("  PASSED: Multiple pointer descriptors loaded correctly\n");
}

int main(void) {
    printf("Coco Stack Map Tests\n");
    printf("====================\n\n");

    test_load_valid_stack_map();
    test_load_invalid_stack_map();
    test_free_null_stack_map();
    test_find_func_map();
    test_addr_in_stack();
    test_multiple_pointers();

    printf("\n=== All Stack Map Tests Passed ===\n");
    return 0;
}
