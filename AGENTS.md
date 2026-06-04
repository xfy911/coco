# AGENTS.md — coco

Stackful coroutine library for C (C11 + platform ASM). Version 2.1.0.

## Build

```bash
cmake -B build
cmake --build build
```

Produces `build/libcoco.a` (static library).

### Build options

| Flag | Purpose |
|------|---------|
| `-DCOCO_BUILD_TESTS=OFF` | Skip tests/examples |
| `-DCOCO_ENABLE_COVERAGE=ON` | Coverage + `make coverage` target |
| `-DCOCO_ENABLE_ASAN=ON` | AddressSanitizer |
| `-DCOCO_ENABLE_TSAN=ON` | ThreadSanitizer |
| `-DCOCO_ENABLE_UBSAN=ON` | UndefinedBehaviorSanitizer |

## Test

```bash
cmake --build build
cd build && ctest --output-on-failure
```

Run a single test: `./build/test_<name>`

Run benchmarks: `./build/bench_<name>` (e.g. `bench_switch`, `bench_channel`, `bench_io`)

Coverage report: `cmake -B build -DCOCO_ENABLE_COVERAGE=ON && cmake --build build && cd build && cmake --build . --target coverage`

## Project layout

| Path | What it is |
|------|-----------|
| `include/coco.h` | Public API — all user-facing types and functions |
| `include/coco_safety.h` | Safety mode config (stack growth) |
| `src/coco_internal.h` | Core internal structs (`coco_coro_t`, `coco_sched_t`, `coco_ctx_t`) |
| `src/core/` | Coroutine lifecycle, context init, stack management, cancellation, preemption |
| `src/sched/` | Scheduler: single-threaded (`sched.c`, `runq.c`) and multi-threaded work-stealing (`global_sched.c`) |
| `src/channel/` | Channels: single-thread (`channel.c`) and multi-thread (`channel_mt.c`) |
| `src/io/` | Event loop + poll backends (epoll, kqueue, io_uring) + multi-thread netpoller |
| `src/timer/` | 4-layer hierarchical timing wheel |
| `src/platform/` | ASM context switch (`ctx_{arch}.S`), preemption, threading wrappers, ABI detection |
| `tests/unit/` | 45 unit tests |
| `tests/benchmark/` | 7 benchmarks |
| `tools/coco_stack_pass/` | LLVM pass for stack map generation (dynamic stack growth) |

## Architecture notes

- **Two scheduling modes**: single-threaded (`coco_sched_run`) and multi-threaded Go-style P/M work-stealing (`coco_global_sched_start`).
- **Thread-local state**: `g_current_sched`, `g_current_coro`, `g_return_ctx` — set during context switch, read by `coco_self()` etc.
- **Context structs are ABI-specific**: `coco_ctx_t` layout differs by arch/OS (see `coco_internal.h`). ASM files must match struct offsets exactly.
- **ASM files** are in `src/platform/{linux,macos,windows}/ctx_{x86_64,arm64}.S`. Each has its own file due to different ABIs and directives. See `docs/ASM_STYLE_GUIDE.md`.
- **io_uring** is auto-detected at build; requires `liburing` dev package. Without it, falls back to epoll.
- **Stack management** has three modes: fixed (default), hot-stack shared (LRU 8×128KB slots), and dynamic growth (SIGSEGV + siglongjmp + stack copy).

## Code conventions

- C11 standard, `-Wall -Wextra -O2`.
- Comments are in Chinese (中文).
- No external dependencies beyond libc, liburing (optional), and ws2_32 (Windows).
- Public API uses `coco_` prefix; internal functions use descriptive names without prefix.
- ASM files must document `coco_ctx_t` offset layout in header comments (enforced by `ASM_STYLE_GUIDE.md`).
