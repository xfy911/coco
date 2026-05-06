<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-06 | Updated: 2026-05-06 -->

# integration

## Purpose
Integration tests for the coco coroutine library. Tests multi-module interactions and real-world usage patterns.

## Key Files
| File | Description |
|------|-------------|
| `test_stack_growth.c` | Tests stack growth detection and handling for coroutines |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Integration tests verify multiple modules work together correctly
- Tests should use the public `coco.h` API
- These tests are more comprehensive than unit tests but slower

### Testing Requirements
- Run after unit tests pass
- Tests may require specific platform configurations
- Some tests may need elevated privileges or specific resource limits

### Common Patterns
- Test scenarios that span multiple modules (e.g., coroutine + channel + timer)
- Real-world usage patterns like producer-consumer, echo server
- Stress tests with many coroutines

## Dependencies

### Internal
- `include/coco.h` — public API
- `src/` — library implementation

### External
- C11 standard library

<!-- MANUAL: Custom project notes can be added below -->
