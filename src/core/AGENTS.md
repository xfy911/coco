<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# core

## Purpose
Core coroutine implementation: lifecycle management, cooperative scheduler, context switching (assembly), stack allocation with guard pages, and signal handling for stack overflow detection.

## Key Files
| File | Description |
|------|-------------|
| `coro.c` | Coroutine lifecycle: spawn, resume, yield, join, cleanup; run queue management; state transitions |
| `sched.c` | Scheduler: event loop integration, run queue dispatch, idle polling, shutdown |
| `context.c` | C fallback context switch (`coco_swap_ctx`); delegates to ASM when available |
| `stack.c` | Stack allocation via mmap with guard pages; stack overflow detection via SIGSEGV handler |
| `signal.c` | SIGSEGV handler for stack overflow detection; sigaltstack setup for safe handler execution |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- `coro.c` is the most critical file — handles coroutine state machine and run queue
- Context switching is performance-sensitive; the C fallback in `context.c` is slower than ASM
- Stack allocation uses `mmap(MAP_ANONYMOUS)` with `mprotect` guard pages
- The scheduler (`sched.c`) integrates with I/O poll and timer wheel for blocking operations
- `g_current_coro` and `g_current_sched` are accessed directly (single-threaded model)

### Testing Requirements
- Context switch correctness must be verified after any change to `context.c` or platform ASM
- Stack overflow detection relies on signal handler setup; test with deep recursion
- Run queue integrity must be maintained; test with many concurrent coroutines

### Common Patterns
- Coroutine states: `READY → RUNNING → BLOCKED/DONE`
- Run queue is a doubly-linked list (`coro->next`, `coro->prev`)
- Blocking operations set `coro->state = BLOCKED` and yield; wake-up re-enqueues to run queue
- `coco_swap_ctx()` saves/restores callee-saved registers and stack pointer

## Dependencies

### Internal
- `src/coco_internal.h` — struct definitions and internal API
- `src/platform/` — assembly context switch implementations
- `src/io/` — I/O poll for scheduler idle
- `src/timer/` — timer wheel for scheduler sleep

### External
- POSIX: mmap, mprotect, sigaltstack, sigaction
- Platform-specific assembly (x86-64, AArch64)

<!-- MANUAL: Custom project notes can be added below -->
