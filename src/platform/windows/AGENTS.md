<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# windows

## Purpose
Windows-specific assembly implementation for coroutine context switching. Currently supports x86-64 only (Microsoft ABI).

## Key Files
| File | Description |
|------|-------------|
| `ctx_x86_64.S` | x86-64 context switch: saves/restores callee-saved registers per Microsoft x64 ABI (rbx, rbp, rsi, rdi, r12-r15) |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Microsoft x64 ABI differs from System V: callee-saved includes rsi, rdi; shadow stack space (32 bytes) required before calls
- First argument in rcx, second in rdx (not rdi/rsi as in System V)
- Stack must be 16-byte aligned before `call`
- Windows does not have ARM64 assembly yet; it would need a separate implementation
- `coco_ctx_switch(current, target)` saves context to `current`, restores from `target`

### Testing Requirements
- Must be tested on Windows x86-64
- Verify Microsoft ABI-specific register preservation
- Verify shadow stack space allocation

### Common Patterns
- Microsoft ABI: rcx=arg1, rdx=arg2, r8=arg3, r9=arg4, then stack
- 32-byte shadow space on stack before calls
- Callee-saved: rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15 (if used)

## Dependencies

### Internal
- `src/coco_internal.h` — `coco_ctx_t` struct definition (register offsets must match)

### External
- MASM or GAS with Windows target

<!-- MANUAL: Custom project notes can be added below -->