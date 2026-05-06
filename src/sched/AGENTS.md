<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-06 | Updated: 2026-05-06 -->

# sched

## Purpose
M:N work-stealing scheduler implementation for multi-threaded coroutine execution. Follows Go runtime design with Processors (P) and Machines (M) abstraction.

## Key Files
| File | Description |
|------|-------------|
| `global_sched.h` | Global scheduler framework: P/M structs, global run queue, processor management |
| `global_sched.c` | Global scheduler implementation: init/destroy, P creation, global queue ops |
| `sched.h` | Scheduler API: find_runnable, schedule_once, load balancing |
| `sched.c` | Core scheduling loop: coroutine dispatch, yield/block/ready transitions |
| `runq.h` | Run queue interface |
| `runq.c` | Local run queue implementation (lock-protected doubly-linked list) |
| `locked_queue.h` | Lock-protected queue interface |
| `locked_queue.c` | Thread-safe queue for global run queue |
| `safepoint.h` | Safepoint mechanism for cooperative preemption |
| `safepoint.c` | Safepoint detection and handling |
| `sched_hooks.h` | Scheduler hook interface |
| `sched_hooks.c` | Hook implementations for debugging/profiling |
| `sched_stats.h` | Scheduler statistics interface |
| `sched_stats.c` | Statistics collection: steal rate, queue depth, latency |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- This is the multi-threaded scheduler (Phase 1); `src/core/sched.c` is the single-threaded version
- P (Processor) holds a local run queue; M (Machine) is an OS thread
- Work stealing: idle P steals from other P's local queues or global queue
- All queue operations must be thread-safe; use pthread mutex
- Safepoints enable cooperative preemption at defined points

### Testing Requirements
- Test with multiple P/M configurations
- Verify work stealing correctness under load
- Check lock contention doesn't cause deadlocks
- Measure steal success rate (should be > 50% under load imbalance)

### Common Patterns
- `find_runnable(p)` — get next coroutine from local queue, global queue, or steal
- `schedule_once(p)` — one iteration of the scheduler loop
- `schedule_ready(g)` — enqueue coroutine to appropriate queue
- Per-P stack pools reduce allocation contention

## Dependencies

### Internal
- `src/coco_internal.h` — coroutine struct definition
- `src/core/` — single-threaded scheduler for comparison
- `src/platform/` — context switching

### External
- `pthread.h` — thread management
- `stdatomic.h` — atomic operations
- `stdint.h`, `stdbool.h` — standard types

<!-- MANUAL: Custom project notes can be added below -->
