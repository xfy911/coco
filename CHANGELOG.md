# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

## [2.2.0] - 2026-06-04

### Added
- Runtime version query API: `coco_version()`, `coco_version_major()`, `coco_version_minor()`, `coco_version_patch()`.
- `ENSURE_IN_CORO` / `ENSURE_IN_CORO_RET` / `ENSURE_IN_CORO_VOID` guard macros for coroutine-only APIs, preventing crashes when called outside a coroutine context.
- POSIX signal block/unblock (`coco_preempt_block_signal()` / `coco_preempt_unblock_signal()`) with reference-counted nesting counter for critical section protection.
- P binding detection for `coco_go()`: when called from a worker thread, the new coroutine binds to the current P instead of scanning all processors.
- Periodic load balancing integrated into the worker loop (triggered every 100 coroutines processed).
- Linux netpoller wakeup uses `eventfd` instead of pipe for lower overhead.
- New examples: `hot_stack`, `dynamic_stack`, `context_api`.
- New benchmarks: `bench_mt_sched`, `bench_hot_cold_switch`; `bench_io` rewritten to measure actual coroutine I/O throughput.
- New tests: `stress_mt_scaling`, `stress_channel_mt_burst`, `test_ctx_x86_64`, `test_preempt_block`, `test_preempt_channel`, `test_preempt_sched`, `test_coro_guard`, `test_sched_reinit`, `test_sched_balanced`.
- Doxygen configuration for API reference generation.
- GitHub Actions CI workflow with sanitizer matrix (ASan, UBSan).
- Automated release workflow triggered on version tag push.

### Changed
- `coco_yield()` return type changed from `void` to `int` (returns `COCO_ERROR_INVALID` when called outside a coroutine). **ABI note:** existing compiled objects must be recompiled.
- `coro->state` type changed from `int` (non-atomic) to `_Atomic int` for thread-safety. **ABI note:** existing compiled objects must be recompiled.
- Global runq wake strategy: targeted `pthread_cond_signal` replaces broadcast to fix thundering herd.

### Fixed
- Multi-threaded scheduler data races and leaks:
  - `coro->state` is now `_Atomic` (acquire/release ordering).
  - `stack_pool_multi` protected by per-pool mutex.
  - Wrong-pool-free on work-stealing fixed via `coro->stack_pool` tracking.
- Channel `wait_queue_lock` and scheduler `global_runq_lock` / `local_runq_lock` critical sections protected from preemption.
- Scheduler reinitialization: `coco_global_sched_stop()` now fully cleans up state (queues, counters, locks).
- `schedule_balanced()` now actually redistributes coroutines from overloaded P's and releases hot-stack slots before migration.
- Worker thread H2 path now performs load balancing (extracted shared `handle_coro_done` helper).
- Windows APC preemption delivery: added alertable `SleepEx` in idle paths.
- Windows worker idle sleep capped to 1ms to prevent lost `pthread_cond_signal` wakeups.
- Windows I/O function linker conflicts between POSIX and Winsock implementations resolved.
- UBSan null-pointer in `stress_channel_burst`: hot-stack backup/restore now rewrites stack pointers in callee-saved registers.
- GCC 16 rbp clobber in sanitizer builds (`-fomit-frame-pointer` for register integrity test).
- Signal blocking nesting counter underflow guard in `coco_preempt_unblock_signal()`.
- Munmap fallback in `coco_global_sched_stop()` queue cleanup when `coro->stack_pool` is NULL.

## [2.1.0] - 2026-06-04

### Fixed

- Memory leak when coroutine IDs exceed `coro_table` capacity (dynamic `realloc` expansion)
- Memory leak in MT worker: dead coroutines not freed after execution
- Memory leak in `stress_channel_burst` test: channels not destroyed on teardown
- Memory leak in `coco_sched_destroy`: select state and non-pool stacks not cleaned up
- io_uring test crash under AddressSanitizer (skipped due to mmap ring buffer incompatibility with ASAN)

### Added

- `cmake --install` support for headers and static library (GNUInstallDirs)
- Shared stack support with work-stealing migration hook
- Stack growth benchmarks and stress tests

## [2.0.0] - 2026-05-09

### Added — Multi-threaded Scheduler

- M:N multi-threaded scheduler core with work-stealing across P threads
- Local run queue (doubly-linked list per P) and global run queue
- `coco_go` API for multi-threaded coroutine creation
- Netpoller integration for coroutine I/O in multi-threaded scheduler
- Multi-threaded Channel support (thread-safe send/receive)
- Multi-threaded HTTP static server example (`http_static_mt`)

### Added — Dynamic Stack Growth

- Opt-in dynamic stack growth with fail-fast validation
- LLVM MachineFunctionPass for stack map generation
- Stack map data structure with binary search API
- Frame pointer traversal mechanism for stack walking
- Signal handler stack growth invocation
- Overflow checkpoint integration in scheduler loop
- `coco_create_safe()` for safe coroutine creation with overflow recovery
- `coco_shrink_stack` with pointer adjustment for stack reclamation
- Conservative stack growth without stack map (fallback mode)
- Default stack size changed to 2KB (Go 1.22+ compatible)
- Multi-size stack pool with reuse and stack usage telemetry

### Added — Preemption & Scheduling

- Async signal-based preemption (≤10ms latency)
- Optional time-slice round-robin fairness
- Multi-level priority scheduling with bitmap O(1) lookup
- Safepoint mechanism for cooperative yielding points
- Coroutine cancellation mechanism (`coco_cancel`)
- O(1) timer cancellation in hierarchical timing wheel
- Scheduler hooks system
- Scheduler statistics API

### Added — Context API

- Context API implementation (`coco_context_t`)
- Context value propagation (key-value store with parent chaining)
- Context cancellation and deadline propagation
- Background and TODO context presets

### Added — I/O

- io_uring high-performance I/O backend (Linux)
- Windows WSAPoll I/O backend (full implementation)
- Batch I/O API for high-throughput scenarios
- I/O backend selection API (`coco_io_set_backend`)
- I/O backend comparison benchmarks
- SQPOLL mode with configurable parameters and syscall statistics
- Common I/O logic extracted into `event_loop.c`

### Added — Platform & ABI

- Windows ARM64 context switch assembly implementation
- Mixed ABI detection mechanism for cross-ABI compatibility
- Platform abstraction layer
- ARM64 context switch ABI compliance tests
- Assembly coding conventions and unified file headers

### Added — Channel & Communication

- `coco_channel_select` for Go-style channel multiplexing
- Channel select unit tests

### Added — Examples & Docs

- HTTP static server example (single-threaded) with:
  - HTTP request parsing and response building
  - Path validation with directory traversal protection
  - Static file serving with chunked transfer
  - Directory handling with autoindex
  - Connection handling with coroutines
  - Zero-copy file transfer via `sendfile`
  - Graceful shutdown during file transfer
- Migration guide for Go-like runtime features
- Detailed usage guide and code examples
- Channel Select section in README API Reference
- Updated platform support matrix and architecture docs

### Changed

- Default stack size changed from 32KB to 2KB
- Stack infrastructure extracted to `stack_common`
- Channel wait queue helpers extracted to `channel_common.h`
- Shared I/O code extracted to `io_internal.h`
- Batch I/O dispatch consolidated through platform layer
- Scheduler `find_runnable` deduplicated

### Removed

- Dead code: `locked_queue`, `stack_pool_legacy`, `core/sched`, unused functions
- Unused safepoint and scheduler hooks infrastructure

### Performance

- Priority queue bitmap for O(1) scheduling decisions
- Stack pool selective zeroing and embedded list nodes
- Channel embedded wait nodes for zero-allocation send/receive
- Timer pool to avoid frequent `calloc`/`free`
- Periodic aging check to reduce `clock_gettime` calls
- epoll O_NONBLOCK cache optimization
- Increased epoll/kqueue batch size

### Fixed

- Multi-threaded scheduler coroutine state management
- MT scheduler SegFault with `g_return_ctx` indirection
- Critical concurrency bugs in channel and scheduler
- Channel double-free and use-after-free in `coco_channel_close`
- Channel select blocking Phase 1 replaced with inline checks
- Netpoller data race and `EPOLL_CTL_ADD`/`MOD` registration order
- `coco_write`/`coco_read` stale `errno` check
- O_NONBLOCK set before first read/write to prevent deadlock
- POSIX includes wrapped in `_WIN32` guards for Windows compatibility
- x86-64 context structure, signal stack thread safety
- Linux ARM64 and Windows x86-64 context switch assembly
- Context switch ABI violations
- Timer wheel memory leak and stack pool reuse issues
- Overflow recovery state handling in signal handler
- `ctx.stack_base`/`stack_limit` synchronization with coroutine fields
- Children array initialization in background/todo contexts
- Compiler truncation warnings and format specifier warnings
- Safepoint test compatibility in non-debug mode
- Racy `active_coroutines` check in MT scheduler test
- HTTP static server: clean shutdown, accept loop interruption, memory leak

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

[Unreleased]: https://github.com/xfy/coco/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/xfy/coco/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/xfy/coco/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/xfy/coco/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/xfy/coco/releases/tag/v1.0.0
