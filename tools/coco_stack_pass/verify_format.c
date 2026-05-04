/**
 * verify_format.c - Verify .coco_stackmap binary format
 * US-217: Binary format verification
 * Supports version 2 format with function names
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Stack map header structure */
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

/* Pointer kinds */
#define KIND_FRAME_PTR   0
#define KIND_RETURN_ADDR 1
#define KIND_LOCAL_PTR   2
#define KIND_SPILL_REG   3
#define KIND_MAYBE_PTR   4

const char* kind_to_string(uint8_t kind) {
    switch (kind) {
        case KIND_FRAME_PTR:   return "FRAME_PTR";
        case KIND_RETURN_ADDR: return "RETURN_ADDR";
        case KIND_LOCAL_PTR:   return "LOCAL_PTR";
        case KIND_SPILL_REG:   return "SPILL_REG";
        case KIND_MAYBE_PTR:   return "MAYBE_PTR";
        default:               return "UNKNOWN";
    }
}

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
    if (header.version < 1 || header.version > 2) {
        printf("ERROR: Unsupported version %u (expected 1 or 2)\n", header.version);
        fclose(f);
        return 1;
    }
    printf("[PASS] Version %u is supported\n", header.version);

    /* Read function entries */
    printf("\nFunction Entries:\n");
    int nonzero_addr_count = 0;

    for (uint32_t i = 0; i < header.num_funcs; i++) {
        /* Read function header */
        uint64_t func_addr, func_size;
        uint32_t frame_size, num_pointers;

        if (fread(&func_addr, sizeof(func_addr), 1, f) != 1 ||
            fread(&func_size, sizeof(func_size), 1, f) != 1 ||
            fread(&frame_size, sizeof(frame_size), 1, f) != 1 ||
            fread(&num_pointers, sizeof(num_pointers), 1, f) != 1) {
            printf("ERROR: Failed to read function %u header\n", i);
            fclose(f);
            return 1;
        }

        /* Read function name (version 2+) */
        char func_name[256] = {0};
        if (header.version >= 2) {
            uint16_t name_len;
            if (fread(&name_len, sizeof(name_len), 1, f) != 1) {
                printf("ERROR: Failed to read function %u name length\n", i);
                fclose(f);
                return 1;
            }
            if (name_len > 0 && name_len < sizeof(func_name)) {
                if (fread(func_name, 1, name_len, f) != name_len) {
                    printf("ERROR: Failed to read function %u name\n", i);
                    fclose(f);
                    return 1;
                }
            }
        }

        printf("\n  Function %u: '%s'\n", i, func_name[0] ? func_name : "(unnamed)");
        printf("    Address:     0x%016lX\n", (unsigned long)func_addr);
        printf("    Size:        %lu bytes\n", (unsigned long)func_size);
        printf("    Frame size:  %u bytes\n", frame_size);
        printf("    Pointers:    %u\n", num_pointers);

        if (func_addr != 0) {
            nonzero_addr_count++;
        }

        /* Read pointer descriptors */
        for (uint32_t j = 0; j < num_pointers; j++) {
            coco_ptr_desc_t ptr;
            if (fread(&ptr, sizeof(ptr), 1, f) != 1) {
                printf("ERROR: Failed to read pointer %u for function %u\n", j, i);
                fclose(f);
                return 1;
            }

            printf("      Pointer %u: offset=%d, size=%u, kind=%s\n",
                   j, ptr.frame_offset, ptr.size, kind_to_string(ptr.kind));
        }
    }

    fclose(f);

    printf("\n=== PASS: Format verified successfully ===\n");
    printf("\nSummary:\n");
    printf("  Functions: %u\n", header.num_funcs);
    printf("  Non-zero addresses: %d/%u\n", nonzero_addr_count, header.num_funcs);

    if (nonzero_addr_count == 0) {
        printf("\nNote: All addresses are 0 (need post-processing with post_process.py)\n");
    } else if (nonzero_addr_count == (int)header.num_funcs) {
        printf("\n[PASS] All function addresses have been resolved\n");
    }

    return 0;
}
