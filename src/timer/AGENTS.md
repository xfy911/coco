<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# timer

## Purpose
Hierarchical timing wheel for efficient timer management. Supports one-shot and repeating timers, sleep, and deadline-based scheduling.

## Key Files
| File | Description |
|------|-------------|
| `timer_wheel.c` | Hierarchical timing wheel: create, add timer, tick, expire; 4-level hierarchy (ms, sec, min, hr) |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- The timing wheel uses a 4-level hierarchy for O(1) add/tick with large timeout ranges
- Each level has 256 slots; cascade from higher levels to lower on tick
- `coco_sleep()` creates a one-shot timer and blocks the calling coroutine
- Timer expiry wakes the blocked coroutine and re-enqueues it to the run queue
- The scheduler calls `coco_timer_tick()` on each iteration to advance the wheel

### Testing Requirements
- Test timer accuracy (within reasonable tolerance for cooperative scheduling)
- Test cascade behavior with long timeouts (> 256ms, > 65s)
- Test repeating timers and timer cancellation
- Verify sleep integration with scheduler

### Common Patterns
- Timer IDs are slot indices within the wheel levels
- `coco_timer_after(duration, callback, arg)` — one-shot timer
- `coco_timer_every(duration, callback, arg)` — repeating timer
- `coco_timer_cancel(timer_id)` — cancel pending timer
- Wheel tick driven by scheduler; no background thread

## Dependencies

### Internal
- `src/coco_internal.h` — timer and coroutine structs
- `src/core/coro.c` — coroutine wake-up on timer expiry

### External
- C11 standard library

<!-- MANUAL: Custom project notes can be added below -->
