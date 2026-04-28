<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# platform

## Purpose
Platform-specific assembly implementations for context switching and platform utilities. Isolates architecture-dependent code from portable C logic.

## Key Files
None at this level (all files are in subdirectories).

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `linux/` | Linux x86-64 and AArch64 assembly context switch (see `linux/AGENTS.md`) |
| `macos/` | macOS AArch64 (Apple Silicon) assembly context switch (see `macos/AGENTS.md`) |
| `windows/` | Windows x86-64 assembly context switch and platform helpers (see `windows/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- Platform code is selected at compile time via CMake platform/arch detection
- Assembly files must preserve ABI conventions (callee-saved registers)
- Each platform directory provides `coco_swap_ctx` implementation
- The C fallback in `src/core/context.c` is used when no ASM is available

### Testing Requirements
- Context switch ASM must be tested on the target platform
- Verify callee-saved register preservation across switches
- Test with different stack sizes and alignments

### Common Patterns
- `coco_swap_ctx(from, to)` — save current context to `from`, restore from `to`
- Context structure: array of registers matching ASM save/restore order
- Stack alignment: 16-byte on x86-64, 16-byte on AArch64

## Dependencies

### Internal
- `src/coco_internal.h` — context struct definition

### External
- Platform-specific assembler (GAS, MASM)

<!-- MANUAL: Custom project notes can be added below -->
