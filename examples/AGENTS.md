<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-05-07 -->

# examples

## Purpose
Usage examples demonstrating the coco coroutine library API and common patterns.

## Key Files
| File | Description |
|------|-------------|
| `basic.c` | Basic coroutine creation, yielding, and scheduler usage |
| `pipeline.c` | Channel-based producer-consumer pipeline pattern |
| `echo_server.c` | Async TCP echo server using I/O multiplexing and coroutines |
| `memory_test.c` | Memory usage and stack telemetry benchmarks |
| `timer.c` | Timer and delayed execution examples |
| `select.c` | Channel select (multiplexing) example |
| `priority.c` | Coroutine priority scheduling example |

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
