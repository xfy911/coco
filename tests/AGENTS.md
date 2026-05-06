<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-05-06 -->

# tests

## Purpose
Unit tests and benchmarks for the coco coroutine library. Validates correctness of coroutines, channels, I/O, timers, and measures performance of context switching and scheduling.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Test build configuration: links against coco, registers test executables with CTest |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `unit/` | Unit tests for each module (see `unit/AGENTS.md`) |
| `integration/` | Integration tests for multi-module interactions (see `integration/AGENTS.md`) |
| `benchmark/` | Performance benchmarks (see `benchmark/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- Run tests via `cd build && ctest --output-on-failure`
- Tests are registered as CTest targets; individual tests can be run by name
- Tests must be rebuilt after any source change: `cmake --build build`

### Testing Requirements
- All tests must pass before any commit
- Benchmark results should be compared against baseline (< 100ns context switch)

### Common Patterns
- Each test file focuses on one module area
- Tests use the public `coco.h` API, not internal headers
- Benchmarks measure wall-clock time using `clock_gettime(CLOCK_MONOTONIC)`

## Dependencies

### Internal
- `include/coco.h` — public API
- `src/` — library implementation (linked as static library)

### External
- CTest — test runner framework
- C11 standard library

<!-- MANUAL: Custom project notes can be added below -->
