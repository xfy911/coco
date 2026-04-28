<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# unit

## Purpose
Unit tests for each module of the coco coroutine library. Validates correctness of coroutines, channels, I/O, timers, and signal handling.

## Key Files
| File | Description |
|------|-------------|
| `test_coro.c` | Tests for coroutine lifecycle: spawn, yield, join, state transitions |
| `test_channel.c` | Tests for channel: send, recv, buffered/unbuffered, close, select |
| `test_io.c` | Tests for I/O: register, wait, wake-up, TCP echo |
| `test_timer.c` | Tests for timer: one-shot, repeating, cancel, sleep integration |
| `test_signal.c` | Tests for signal handling: stack overflow detection |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Tests use the public `coco.h` API
- Each test file is a separate executable registered with CTest
- Tests create a scheduler, spawn coroutines, and verify behavior
- Use `ASSERT_*` macros for test assertions

### Testing Requirements
- Run via `cd build && ctest --output-on-failure`
- All tests must pass before committing
- Add new tests when adding new features or fixing bugs

### Common Patterns
- Test setup: `coco_sched_create()` → `coco_spawn()` → `coco_sched_run()`
- Test teardown: scheduler auto-frees when all coroutines complete
- Assertions check return values, coroutine state, and side effects

## Dependencies

### Internal
- `include/coco.h` — public API

### External
- CTest — test runner
- C11 standard library

<!-- MANUAL: Custom project notes can be added below -->
