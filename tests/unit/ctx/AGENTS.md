<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-06 | Updated: 2026-05-06 -->

# ctx

## Purpose
Context switching unit tests for different CPU architectures. Validates assembly and C fallback implementations.

## Key Files
| File | Description |
|------|-------------|
| `test_ctx_arm64.c` | ARM64-specific context switch tests: register preservation, stack alignment |
| `test_ctx_common.h` | Shared test utilities and assertions for context tests |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Context switch tests are architecture-specific
- Tests verify callee-saved registers are preserved across switches
- Stack alignment must be maintained (16-byte on ARM64)
- Both ASM and C fallback implementations should pass the same tests

### Testing Requirements
- Run on target architecture (ARM64 tests require ARM64 hardware)
- Compare ASM vs C fallback behavior
- Verify no register corruption after many switches

### Common Patterns
- Test pattern: save registers → switch context → verify registers
- Stress test: thousands of context switches in sequence
- Edge cases: switch with minimal stack, switch from signal handler

## Dependencies

### Internal
- `src/platform/` — assembly context switch implementations
- `src/core/context.c` — C fallback implementation

### External
- Architecture-specific headers (no external libraries)

<!-- MANUAL: Custom project notes can be added below -->
