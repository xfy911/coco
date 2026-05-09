<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-05-09 -->

# examples

## Purpose
Usage examples demonstrating the coco coroutine library API and common patterns.

## Key Files
| File | Description |
|------|-------------|
| `basic.c` | Basic coroutine creation, yielding, and scheduler usage |
| `pipeline.c` | Channel-based producer-consumer pipeline pattern |
| `echo_server.c` | Async TCP echo server using I/O multiplexing and coroutines |
| `http_static.c` | Single-threaded HTTP static file server with sendfile, chunked transfer, and graceful shutdown |
| `http_static_mt.c` | Multi-threaded HTTP static file server using M:N scheduler and netpoller |
| `memory_test.c` | Memory usage and stack telemetry benchmarks |
| `timer.c` | Timer creation, cancellation, and `coco_sleep` usage |
| `select.c` | Channel select (multiplexing) example |
| `priority.c` | Coroutine priority scheduling example |
| `cancel.c` | Coroutine cancellation with `coco_cancel`/`coco_cancelled` |
| `join_exit.c` | `coco_join` for result waiting and `coco_exit` for return values |
| `preemption.c` | Signal-based preemption, fairness scheduling, and cooperative checkpoints |
| `fan_out.c` | Fan-out/fan-in parallel pattern using channels |
| `sched_run_once.c` | Step-by-step scheduler control with `coco_sched_run_once` |
| `multithread.c` | Multi-threaded M:N scheduler with `coco_go`/`coco_go_on`/`coco_go_with_opts` |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Examples are built by CMake as separate executables
- Each example is self-contained and demonstrates a specific feature area
- Examples serve as integration tests; they should compile and run after any API change

### Testing Requirements
- Verify examples compile and run after API changes
- Examples should not require external dependencies beyond the coco library

### Common Patterns
- `coco_create()` to create coroutines
- `coco_sched_run()` to start the scheduler
- `coco_channel_create()` / `coco_channel_send()` / `coco_channel_recv()` for communication
- `coco_sleep()` / `coco_timer()` for timed operations
- `coco_channel_select()` for multiplexing multiple channels

## Dependencies

### Internal
- `include/coco.h` — public API

### External
- CMake (built via top-level CMakeLists.txt)
