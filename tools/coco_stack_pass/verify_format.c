/**
 * verify_format.c - Verify .coco_stackmap binary format
 * US-217: Binary format verification
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Stack map header structure (matches coco_stack_map.h) */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_funcs;
} coco_stackmap_header_t;

/* Pointer descriptor (matches coco_ptr_desc_t) */
typedef struct {
    int32_t frame_offset;
    uint16_t size;
    uint8_t kind;
    uint8_t flags;
} coco_ptr_desc_t;

/* Function map entry header */
typedef struct {
    uint64_t func_addr;
    uint64_t func_size;
    uint32_t frame_size;
    uint32_t num_pointers;
} coco_func_map_header_t;

/* Pointer kinds */
#define KIND_FRAME_PTR   0
#define KIND_RETURN_ADDR 1
#define KIND_LOCAL_PTR   2
#define KIND_SPILL_REG   3
#define KIND_MAYBE_PTR   4

int main(int argc, char *argv[]) {
    const char *path = "output.coco_stackmap";
    if (argc > 1) {
        path = argv[1];
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("ERROR: Cannot open %s\n", path);
        return 1;
    }

    /* Read header */
    coco_stackmap_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        printf("ERROR: Failed to read header\n");
        fclose(f);
        return 1;
    }

    printf("=== Coco Stack Map Verification ===\n\n");
    printf("Header:\n");
    printf("  Magic:   0x%04X (expected 0xC0C0)\n", header.magic);
    printf("  Version: %u\n", header.version);
    printf("  Functions: %u\n\n", header.num_funcs);

    /* Verify magic */
    if (header.magic != 0xC0C0) {
        printf("ERROR: Magic mismatch! Expected 0xC0C0, got 0x%04X\n", header.magic);
        fclose(f);
        return 1;
    }
    printf("[PASS] Magic is correct\n");

    /* Verify version */
    if (header.version != 1) {
        printf("ERROR: Unsupported version %u (expected 1)\n", header.version);
        fclose(f);
        return 1;
    }
    printf("[PASS] Version is correct\n");

    /* Read function entries */
    printf("\nFunction Entries:\n");
    for (uint32_t i = 0; i < header.num_funcs; i++) {
        coco_func_map_header_t func;
        if (fread(&func, sizeof(func), 1, f) != 1) {
            printf("ERROR: Failed to read function %u header\n", i);
            fclose(f);
            return 1;
        }

        printf("\n  Function %u:\n", i);
        printf("    Address:     0x%016lX\n", (unsigned long)func.func_addr);
        printf("    Size:        %lu bytes\n", (unsigned long)func.func_size);
        printf("    Frame size:  %u bytes\n", func.frame_size);
        printf("    Pointers:    %u\n", func.num_pointers);

        /* Read pointer descriptors */
        for (uint32_t j = 0; j < func.num_pointers; j++) {
            coco_ptr_desc_t ptr;
            if (fread(&ptr, sizeof(ptr), 1, f) != 1) {
                printf("ERROR: Failed to read pointer %u for function %u\n", j, i);
                fclose(f);
                return 1;
            }

            const char *kind_str = "UNKNOWN";
            switch (ptr.kind) {
                case KIND_FRAME_PTR:   kind_str = "FRAME_PTR"; break;
                case KIND_RETURN_ADDR: kind_str = "RETURN_ADDR"; break;
                case KIND_LOCAL_PTR:   kind_str = "LOCAL_PTR"; break;
                case KIND_SPILL_REG:   kind_str = "SPILL_REG"; break;
                case KIND_MAYBE_PTR:   kind_str = "MAYBE_PTR"; break;
            }

            printf("      Pointer %u: offset=%d, size=%u, kind=%s\n",
                   j, ptr.frame_offset, ptr.size, kind_str);
        }
    }

    fclose(f);

    printf("\n=== PASS: Format verified successfully ===\n");
    printf("\nNotes:\n");
    printf("  - Function addresses are 0 (need post-link processing)\n");
    printf("  - Frame sizes may be 0 (IR-level analysis limitation)\n");
    printf("  - For accurate frame offsets, use MachineFunctionPass\n");

    return 0;
}
