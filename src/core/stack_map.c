/**
 * stack_map.c - Stack map implementation for dynamic stack growth
 *
 * Implements binary search lookup and memory management for stack maps.
 *
 * @file stack_map.c
 * @brief Stack map runtime implementation
 */

#include "coco_stack_map.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define COCO_STACK_MAP_MAGIC 0xC0C0

/**
 * Binary search to find function containing the given address.
 *
 * The function entries are sorted by func_addr for efficient lookup.
 */
coco_func_map_t* coco_find_func_map(const coco_stack_map_t* map, uint64_t addr) {
    if (!map || map->magic != COCO_STACK_MAP_MAGIC || map->num_funcs == 0) {
        return NULL;
    }

    // Binary search
    uint32_t left = 0;
    uint32_t right = map->num_funcs;
    coco_func_map_t* base = (coco_func_map_t*)map->funcs;

    while (left < right) {
        uint32_t mid = left + (right - left) / 2;

        // Calculate offset to mid entry (variable-sized entries)
        coco_func_map_t* entry = base;
        for (uint32_t i = 0; i < mid; i++) {
            // Skip to next entry based on current entry's size
            size_t entry_size = sizeof(coco_func_map_t) +
                (entry->num_pointers - 1) * sizeof(coco_ptr_desc_t);
            entry = (coco_func_map_t*)((char*)entry + entry_size);
        }

        if (addr < entry->func_addr) {
            right = mid;
        } else if (addr >= entry->func_addr + entry->func_size) {
            left = mid + 1;
        } else {
            // Found: addr is within [func_addr, func_addr + func_size)
            return entry;
        }
    }

    return NULL;
}

/**
 * Load stack map from file.
 *
 * The file format is:
 * - Header: magic (4), version (4), num_funcs (4)
 * - For each function:
 *   - func_addr (8), func_size (8), frame_size (4), num_pointers (4)
 *   - Pointer descriptors: num_pointers * sizeof(coco_ptr_desc_t)
 */
coco_stack_map_t* coco_load_stack_map(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    // Read header
    uint32_t header[3];
    if (fread(header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return NULL;
    }

    if (header[0] != COCO_STACK_MAP_MAGIC) {
        fclose(f);
        return NULL;
    }

    uint32_t num_funcs = header[2];

    // Calculate total size (approximate - need to read entries to get exact size)
    // Start with a reasonable buffer size
    size_t buffer_size = sizeof(coco_stack_map_t) + num_funcs * sizeof(coco_func_map_t) +
        num_funcs * 10 * sizeof(coco_ptr_desc_t); // Estimate ~10 pointers per function

    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    // Write header to buffer
    coco_stack_map_t* map = (coco_stack_map_t*)buffer;
    map->magic = header[0];
    map->version = header[1];
    map->num_funcs = num_funcs;

    // Read function entries
    char* current = buffer + sizeof(coco_stack_map_t) - sizeof(coco_func_map_t);
    for (uint32_t i = 0; i < num_funcs; i++) {
        coco_func_map_t* entry = (coco_func_map_t*)current;

        // Read function metadata
        if (fread(&entry->func_addr, sizeof(uint64_t), 1, f) != 1 ||
            fread(&entry->func_size, sizeof(uint64_t), 1, f) != 1 ||
            fread(&entry->frame_size, sizeof(uint32_t), 1, f) != 1 ||
            fread(&entry->num_pointers, sizeof(uint32_t), 1, f) != 1) {
            free(buffer);
            fclose(f);
            return NULL;
        }

        // Read pointer descriptors
        if (fread(entry->pointers, sizeof(coco_ptr_desc_t), entry->num_pointers, f) !=
            entry->num_pointers) {
            free(buffer);
            fclose(f);
            return NULL;
        }

        // Move to next entry
        size_t entry_size = sizeof(coco_func_map_t) +
            (entry->num_pointers - 1) * sizeof(coco_ptr_desc_t);
        current += entry_size;
    }

    fclose(f);
    return map;
}

/**
 * Free loaded stack map.
 */
void coco_free_stack_map(coco_stack_map_t* map) {
    if (map) {
        free(map);
    }
}

/**
 * Check if address is within stack range.
 */
bool coco_addr_in_stack(uintptr_t stack_base, uintptr_t stack_limit, uintptr_t addr) {
    // Stack grows downward: stack_base is lowest address, stack_limit is highest
    return addr >= stack_base && addr < stack_limit;
}