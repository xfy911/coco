<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# src

## Purpose
Core implementation of the coco coroutine library. Contains the scheduler, coroutine management, context switching, channel communication, I/O multiplexing, timer wheel, and platform-specific code.

## Key Files
| File | Description |
|------|-------------|
| `coco_internal.h` | Shared internal header: struct definitions, global state declarations, internal API prototypes |

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `core/` | Coroutine lifecycle, scheduler, context switching, stack management, signal handling (see `core/AGENTS.md`) |
| `channel/` | Go-style channel communication (see `channel/AGENTS.md`) |
| `io/` | Async I/O event loop and platform-specific pollers (see `io/AGENTS.md`) |
| `timer/` | Hierarchical timing wheel (see `timer/AGENTS.md`) |
| `platform/` | Platform-specific assembly and utilities (see `platform/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- `coco_internal.h` is the central internal header included by all .c files
- Global state: `g_current_sched` and `g_current_coro` are thread-local in multi-threaded builds, global in single-threaded
- The scheduler is non-preemptive; coroutines must explicitly yield or block on I/O/channel/timer
- All blocking operations (channel send/recv, I/O wait, sleep) internally yield to the scheduler

### Testing Requirements
- Changes to `coco_internal.h` affect all modules; rebuild and run full test suite
- Context switch correctness is critical; always verify with unit tests after changes

### Common Patterns
- Doubly-linked list for run queue (`coro->next`, `coro->prev`)
- Wait queues for channels and I/O (linked list of blocked coroutines)
- Guard pages on coroutine stacks for overflow detection
- `coro->state` tracks lifecycle: READY → RUNNING → BLOCKED/DONE

## Dependencies

### Internal
- `include/coco.h` — public API definitions

### External
- POSIX APIs (mmap, mprotect, sigaltstack, epoll/kqueue)
- Platform-specific assembly (x86-64, AArch64)

<!-- MANUAL: Custom project notes can be added below -->
