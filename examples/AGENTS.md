<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# examples

## Purpose
Usage examples demonstrating the coco coroutine library API and common patterns.

## Key Files
| File | Description |
|------|-------------|
| `basic.c` | Basic coroutine creation, yielding, and scheduler usage |
| `channel.c` | Channel-based producer-consumer communication between coroutines |
| `echo_server.c` | Async TCP echo server using I/O multiplexing and coroutines |
| `timer.c` | Timer and sleep usage with coroutine scheduling |

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
- `coco_spawn()` to create coroutines
- `coco_sched_run()` to start the scheduler
- `coco_channel_create()` / `coco_channel_send()` / `coco_channel_recv()` for communication
- `coco_sleep()` / `coco_timer_after()` for timed operations

## Dependencies

### Internal
- `include/coco.h` — public API

### External
- CMake (built via top-level CMakeLists.txt)

<!-- MANUAL: Custom project notes can be added below -->
