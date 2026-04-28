<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# linux

## Purpose
Linux-specific assembly implementations for coroutine context switching. Supports x86-64 (System V ABI) and ARM64 (AArch64) architectures.

## Key Files
| File | Description |
|------|-------------|
| `ctx_x86_64.S` | x86-64 context switch: saves/restores callee-saved registers (rbx, rbp, r12-r15) per System V ABI |
| `ctx_arm64.S` | ARM64 context switch: saves/restores callee-saved registers (x19-x28, fp, lr) per AArch64 ABI |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Assembly must match the `coco_ctx_t` struct layout in `coco_internal.h`
- System V ABI: callee-saved registers are rbx, rbp, r12-r15; stack must be 16-byte aligned before `call`
- AArch64 ABI: callee-saved registers are x19-x28, x29 (fp), x30 (lr); stack must be 16-byte aligned
- `coco_ctx_switch(current, target)` saves context to `current`, restores from `target`
- First argument in rdi/x0, second in rsi/x1

### Testing Requirements
- Must be tested on actual Linux hardware (x86-64 and ARM64)
- Verify register preservation across context switches
- Verify stack alignment requirements

### Common Patterns
- Function prologue: save callee-saved registers to context struct
- Function epilogue: restore callee-saved registers from context struct
- Return address stored in lr (ARM64) or on stack (x86-64)

## Dependencies

### Internal
- `src/coco_internal.h` — `coco_ctx_t` struct definition (register offsets must match)

### External
- GAS assembler (GNU Assembler)

<!-- MANUAL: Custom project notes can be added below -->