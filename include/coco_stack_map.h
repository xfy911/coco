/**
 * coco_stack_map.h - Stack map data structures for dynamic stack growth
 *
 * This header defines the data structures used by the CocoStackPass
 * LLVM plugin and the runtime stack growth mechanism.
 *
 * @file coco_stack_map.h
 * @brief Stack map types and API for dynamic stack growth
 */

#ifndef COCO_STACK_MAP_H
#define COCO_STACK_MAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Stack Size Constants === */
/** @defgroup StackSizeConstants Stack Size Constants
 *  @brief Constants for dynamic stack growth limits
 *  @{
 */

/** Minimum stack size (2KB) - starting size for new coroutines */
#define COCO_STACK_MIN_SIZE     2048

/** Conservative max stack size (64KB) - upper limit for CONSERVATIVE mode */
#define COCO_STACK_CONSERVATIVE_MAX (64 * 1024)

/** Maximum stack size (1MB) - upper limit for FULL mode */
#define COCO_STACK_MAX_SIZE     (1024 * 1024)

/** Default initial stack size (64KB) - matches current coco default */
#define COCO_STACK_DEFAULT_SIZE (64 * 1024)

/** Stack growth threshold - grow when usage exceeds this percentage */
#define COCO_STACK_GROW_THRESHOLD 0.75

/** Stack shrink threshold - shrink when usage falls below this percentage */
#define COCO_STACK_SHRINK_THRESHOLD 0.25

/** @} */

/* === Pointer Kinds === */
/** @defgroup PointerKinds Pointer Kinds
 *  @brief Classification of pointers found in stack frames
 *  @{
 */

/** Frame pointer - the base of the current stack frame */
#define COCO_PTR_FRAME_PTR      0

/** Return address - saved instruction pointer */
#define COCO_PTR_RETURN_ADDR    1

/** Local pointer - pointer to a local variable */
#define COCO_PTR_LOCAL_PTR      2

/** Spill register - register spilled to stack */
#define COCO_PTR_SPILL_REG      3

/** Maybe pointer - potential pointer (8-byte aligned) */
#define COCO_PTR_MAYBE_PTR      4

/** Unknown pointer type */
#define COCO_PTR_UNKNOWN        0xFF

/** @} */

/* === Data Structures === */

/**
 * @struct coco_ptr_desc_t
 * @brief Descriptor for a pointer found in a stack frame
 *
 * This structure describes a single pointer within a function's stack frame.
 * The offset is relative to the frame pointer (FP/rbp/x29).
 */
typedef struct coco_ptr_desc {
    /** Offset from frame pointer (positive = above FP, negative = below FP) */
    int32_t  frame_offset;

    /** Size of the pointer/object (typically 4 or 8 bytes) */
    uint16_t size;

    /** Kind of pointer (see COCO_PTR_* constants) */
    uint8_t  kind;

    /** Additional flags (reserved for future use) */
    uint8_t  flags;
} coco_ptr_desc_t;

/**
 * @struct coco_func_map_t
 * @brief Stack map entry for a single function
 *
 * Contains all pointer descriptors for a function's stack frame,
 * along with function metadata for runtime lookup.
 */
typedef struct coco_func_map {
    /** Function start address (for lookup) */
    uint64_t func_addr;

    /** Function size in bytes */
    uint64_t func_size;

    /** Total stack frame size */
    uint32_t frame_size;

    /** Number of pointers in this function's frame */
    uint32_t num_pointers;

    /** Array of pointer descriptors (flexible array member) */
    coco_ptr_desc_t pointers[1];
} coco_func_map_t;

/**
 * @struct coco_stack_map_t
 * @brief Complete stack map for a module/program
 *
 * Header structure for the .coco_stackmap file format.
 */
typedef struct coco_stack_map {
    /** Magic number: 0xC0C0 */
    uint32_t magic;

    /** Format version */
    uint32_t version;

    /** Number of function entries */
    uint32_t num_funcs;

    /** Array of function map entries (flexible array member) */
    coco_func_map_t funcs[1];
} coco_stack_map_t;

/* === API Functions === */

/**
 * @brief Find the stack map entry for a given instruction address
 *
 * Uses binary search to efficiently locate the function containing
 * the given address.
 *
 * @param map The complete stack map structure
 * @param addr The instruction address to look up
 * @return Pointer to the function map entry, or NULL if not found
 */
coco_func_map_t* coco_find_func_map(const coco_stack_map_t* map, uint64_t addr);

/**
 * @brief Load stack map from a file
 *
 * @param path Path to the .coco_stackmap file
 * @return Loaded stack map structure, or NULL on error
 */
coco_stack_map_t* coco_load_stack_map(const char* path);

/**
 * @brief Free a loaded stack map
 *
 * @param map The stack map to free
 */
void coco_free_stack_map(coco_stack_map_t* map);

/**
 * @brief Check if an address is within a coroutine's stack range
 *
 * @param stack_base Base address of the coroutine's stack
 * @param stack_limit Current limit of the coroutine's stack
 * @param addr The address to check
 * @return true if addr is within [stack_base, stack_limit)
 */
bool coco_addr_in_stack(uintptr_t stack_base, uintptr_t stack_limit, uintptr_t addr);

#ifdef __cplusplus
}
#endif

#endif /* COCO_STACK_MAP_H */