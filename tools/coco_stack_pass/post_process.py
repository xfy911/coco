#!/usr/bin/env python3
# post_process.py - Post-process .coco_stackmap to fill in function addresses
# US-220: Link-time address resolution for stack map
#
# Usage: ./post_process.py <executable> <stackmap_file> [output_file]
#
# This script reads the symbol table from the executable and updates
# func_addr and func_size fields in the .coco_stackmap binary file.
#
# Supports version 2 format with function names.

import sys
import struct
import subprocess
import os

def read_symbols(executable):
    """Read function addresses from executable's symbol table using nm."""
    func_info = {}

    # Try nm -S first (shows size)
    try:
        result = subprocess.run(
            ['nm', '-S', executable],
            capture_output=True, text=True, check=True
        )
        for line in result.stdout.split('\n'):
            parts = line.split()
            if len(parts) >= 4 and parts[2] == 'T':
                addr, size, _, name = parts[0], parts[1], parts[2], parts[3]
                # Strip leading underscore (common on macOS)
                clean_name = name.lstrip('_')
                func_info[clean_name] = (int(addr, 16), int(size, 16))
                print(f"  Found: {clean_name} @ 0x{addr} (size: 0x{size})")
        return func_info
    except subprocess.CalledProcessError:
        pass

    # Fallback: nm without size
    result = subprocess.run(
        ['nm', executable],
        capture_output=True, text=True, check=True
    )
    for line in result.stdout.split('\n'):
        parts = line.split()
        if len(parts) >= 3 and parts[1] == 'T':
            addr, _, name = parts[0], parts[1], parts[2]
            clean_name = name.lstrip('_')
            func_info[clean_name] = (int(addr, 16), 0)
            print(f"  Found: {clean_name} @ 0x{addr} (size: unknown)")

    return func_info

def process_stack_map(executable, stackmap_path, output_path):
    """Process stack map file and update function addresses."""

    print(f"=== Coco Stack Map Post-Processor ===")
    print(f"Executable: {executable}")
    print(f"Stack map: {stackmap_path}")
    print(f"Output: {output_path}")
    print()

    # Read symbol table
    print("Reading symbol table...")
    func_info = read_symbols(executable)
    print(f"\nFound {len(func_info)} functions in symbol table")

    # Read stack map file
    with open(stackmap_path, 'rb') as f:
        data = bytearray(f.read())

    # Parse header
    magic, version, num_funcs = struct.unpack_from('<III', data, 0)
    print(f"\nStack map header: magic=0x{magic:04X}, version={version}, num_funcs={num_funcs}")

    if magic != 0xC0C0:
        print(f"ERROR: Invalid magic number 0x{magic:04X}")
        sys.exit(1)

    if version < 2:
        print("ERROR: Stack map version 1 does not include function names")
        print("Please recompile with CocoStackPass version 2 or later")
        sys.exit(1)

    # Process each function entry
    offset = 12  # After header (3 x 4 bytes)
    updated_count = 0
    not_found = []

    for i in range(num_funcs):
        # Read function entry header
        func_addr, func_size, frame_size, num_pointers = struct.unpack_from('<QQII', data, offset)

        # Read function name (version 2+)
        name_offset = offset + 24  # After func header (8+8+4+4)
        name_len = struct.unpack_from('<H', data, name_offset)[0]
        func_name = data[name_offset + 2:name_offset + 2 + name_len].decode('utf-8')

        print(f"\nFunction {i}: '{func_name}'")
        print(f"  Current: addr=0x{func_addr:016X}, size={func_size}")

        # Look up address in symbol table
        if func_name in func_info:
            new_addr, new_size = func_info[func_name]

            # Patch the address and size
            struct.pack_into('<Q', data, offset, new_addr)
            struct.pack_into('<Q', data, offset + 8, new_size)

            print(f"  Updated: addr=0x{new_addr:016X}, size=0x{new_size:X}")
            updated_count += 1
        else:
            print(f"  WARNING: Function not found in symbol table")
            not_found.append(func_name)

        # Move to next function entry
        ptr_offset = name_offset + 2 + name_len  # After name
        ptr_size = 8  # CocoPtrDesc = int32_t(4) + uint16_t(2) + uint8_t(1) + uint8_t(1) = 8 bytes
        offset = ptr_offset + (num_pointers * ptr_size)

    print(f"\n=== Summary ===")
    print(f"Updated: {updated_count}/{num_funcs} functions")

    if not_found:
        print(f"Not found: {len(not_found)}")
        for name in not_found:
            print(f"  - {name}")

    # Write output
    with open(output_path, 'wb') as f:
        f.write(data)

    print(f"\nOutput written to: {output_path}")
    return updated_count, num_funcs

def main():
    if len(sys.argv) < 3:
        print("Usage: post_process.py <executable> <stackmap_file> [output_file]")
        print()
        print("Arguments:")
        print("  executable    Path to the compiled executable")
        print("  stackmap_file Path to the .coco_stackmap file")
        print("  output_file   Optional output path (default: overwrite input)")
        sys.exit(1)

    executable = sys.argv[1]
    stackmap_path = sys.argv[2]
    output_path = sys.argv[3] if len(sys.argv) > 3 else stackmap_path

    if not os.path.exists(executable):
        print(f"ERROR: Executable not found: {executable}")
        sys.exit(1)

    if not os.path.exists(stackmap_path):
        print(f"ERROR: Stack map file not found: {stackmap_path}")
        sys.exit(1)

    updated, total = process_stack_map(executable, stackmap_path, output_path)
    print("\n=== Post-processing complete ===")

if __name__ == '__main__':
    main()
