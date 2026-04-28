<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# coco

## Purpose
A production-grade, high-performance, cross-platform C coroutine library. Provides stackful coroutines with cooperative scheduling, Go-style channel communication, async I/O multiplexing, and hierarchical timing wheels. Context switch overhead < 100ns.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Build configuration: C11 + ASM, platform/arch detection, static library, tests, benchmarks, examples |
| `README.md` | Project overview, API reference, architecture diagram, build/test instructions |
| `.gitignore` | Git ignore rules |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `include/` | Public API header (see `include/AGENTS.md`) |
| `src/` | Library implementation (see `src/AGENTS.md`) |
| `examples/` | Usage examples (see `examples/AGENTS.md`) |
| `tests/` | Unit tests and benchmarks (see `tests/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- Build with `cmake -B build && cmake --build build`
- Run tests with `cd build && ctest --output-on-failure`
- The library is C11 with platform-specific assembly (ASM)
- Always verify builds after modifying any source file
- The project uses a single-threaded cooperative model; no thread safety concerns within the scheduler

### Testing Requirements
- All unit tests must pass before committing
- Run benchmarks manually to check for performance regressions
- Context switch benchmark should stay < 100ns

### Common Patterns
- Stackful coroutines with mmap-allocated stacks and guard pages
- Single-threaded event loop with run queue (doubly-linked list)
- Platform-specific code isolated in `src/platform/` and `src/io/poll_*.c`
- Global scheduler/coroutine pointers (`g_current_sched`, `g_current_coro`) for single-threaded access

## Dependencies

### External
- POSIX APIs: mmap/mprotect (stack allocation), sigaltstack (overflow detection), epoll/kqueue (I/O)
- C11 standard library
- CMake 3.16+

<!-- MANUAL: Custom project notes can be added below -->
