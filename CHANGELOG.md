# Changelog

All notable changes to this project will be documented in this file.

## [1.0.0] - 2026-04-28

### Added

- Stackful coroutine with cooperative scheduling
- Stack management with mmap allocation and guard page overflow detection
- Stack overflow signal handling (SIGSEGV + sigaltstack)
- ARM64 and x86-64 context switch assembly (macOS, Linux, Windows)
- Coroutine lifecycle: create, yield, exit, join, destroy
- Coroutine query: self, get_state, get_id, set_error_cb
- Scheduler with run queue (doubly-linked list) and main context switch
- Single-step scheduling via `coco_sched_run_once`
- 4-layer hierarchical timing wheel (1ms precision)
- Channel communication: buffered and unbuffered (Go-style)
- Async I/O: `coco_read`, `coco_write`, `coco_accept`, `coco_connect`, `coco_sleep`
- macOS kqueue event loop
- Linux epoll event loop
- Windows WSAPoll event loop (stub)
- Unit tests: coro, channel, timer, io, stack, stack_overflow
- Benchmarks: context switch, channel throughput, I/O
- Examples: basic coroutine, pipeline, echo server
- AGENTS.md documentation coverage across all subdirectories

### Fixed

- Timer/sleep integration bug
- Format specifier warnings
- Channel use-after-free and scheduler blocking issue
- Linux x86-64 assembly implementation
