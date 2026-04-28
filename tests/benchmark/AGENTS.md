<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# benchmark

## Purpose
Performance benchmarks measuring context switch overhead, scheduler throughput, channel latency, and timer accuracy.

## Key Files
| File | Description |
|------|-------------|
| `bench_ctx_switch.c` | Measures raw context switch time (coroutine yield/resume cycle) |
| `bench_sched.c` | Measures scheduler throughput (coroutines spawned and completed per second) |
| `bench_channel.c` | Measures channel send/recv latency and throughput |
| `bench_timer.c` | Measures timer accuracy and overhead |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Benchmarks are built as separate executables
- Run manually; not part of CTest (they measure performance, not correctness)
- Context switch benchmark target: < 100ns per switch
- Use `clock_gettime(CLOCK_MONOTONIC)` for timing

### Testing Requirements
- Compare benchmark results against baseline after performance-sensitive changes
- Context switch overhead should not regress beyond 100ns

### Common Patterns
- Warm-up iterations before measurement
- Multiple runs with median/percentile reporting
- `clock_gettime(CLOCK_MONOTONIC)` for wall-clock timing
- Results printed to stdout in human-readable format

## Dependencies

### Internal
- `include/coco.h` — public API

### External
- C11 standard library (time.h)

<!-- MANUAL: Custom project notes can be added below -->
