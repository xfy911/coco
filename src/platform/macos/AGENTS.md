<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# macos

## Purpose
macOS-specific assembly implementations for coroutine context switching. Supports x86-64 and ARM64 (Apple Silicon M1/M2/M3) architectures.

## Key Files
| File | Description |
|------|-------------|
| `ctx_x86_64.S` | x86-64 context switch: saves/restores callee-saved registers per macOS System V ABI variant |
| `ctx_arm64.S` | ARM64 context switch: saves/restores callee-saved registers per Apple AArch64 ABI |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- macOS x86-64 follows System V ABI (same as Linux x86-64)
- macOS ARM64 follows Apple's AArch64 ABI: callee-saved x19-x28, x29 (fp), x30 (lr)
- Stack must be 16-byte aligned before function calls on both architectures
- `coco_ctx_switch(current, target)` saves context to `current`, restores from `target`
- Apple Silicon (M1+) is the primary target for ARM64

### Testing Requirements
- Must be tested on macOS (Intel and Apple Silicon)
- Verify register preservation across context switches
- Verify stack alignment requirements

### Common Patterns
- Same register save/restore pattern as Linux, but macOS-specific assembler directives
- Apple assembler uses different section/symbol syntax than GAS

## Dependencies

### Internal
- `src/coco_internal.h` — `coco_ctx_t` struct definition (register offsets must match)

### External
- Apple assembler (as)

<!-- MANUAL: Custom project notes can be added below -->