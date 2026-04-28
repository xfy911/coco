# Coco v2.0 Migration Guide

This guide documents breaking changes when upgrading from v1.x to v2.0.

## Breaking Changes

### 1. Timer API Changes

#### `coco_timer()` Return Type

**v1.x:**
```c
void coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg);
```

**v2.0:**
```c
coco_timer_t *coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg);
```

**Migration:** The function now returns a timer handle that can be used for O(1) cancellation. If you don't need cancellation, simply ignore the return value.

```c
// v1.x
coco_timer(1000, my_callback, NULL);

// v2.0
coco_timer(1000, my_callback, NULL);  // Still works, return value optional
```

#### New Timer Cancellation

**v2.0** introduces O(1) timer cancellation:

```c
coco_timer_t *timer = coco_timer(5000, my_callback, arg);
// ... later ...
coco_timer_cancel(timer);  // O(1) operation
```

### 2. Per-Scheduler Resources

#### File Descriptor Table

**v1.x:** Global FD table shared across all schedulers.

**v2.0:** Each scheduler has its own FD table for isolation.

**Migration:** Code that created multiple schedulers and expected FD sharing needs to be restructured. This is rarely an issue for single-scheduler applications.

#### Signal Handling

**v1.x:** Global signal handlers.

**v2.0:** Signal handling is per-scheduler.

**Migration:** If you relied on global signal state across schedulers, restructure to handle signals within each scheduler context.

### 3. Coroutine Priorities

**v2.0** introduces priority-based scheduling:

```c
// New APIs
void coco_set_priority(coco_coro_t *coro, coco_priority_t priority);
coco_priority_t coco_get_priority(coco_coro_t *coro);
```

Priority levels:
- `COCO_PRIORITY_HIGH` - Real-time tasks, critical paths
- `COCO_PRIORITY_NORMAL` - Default priority
- `COCO_PRIORITY_LOW` - Background tasks
- `COCO_PRIORITY_IDLE` - Only runs when no other tasks

### 4. Coroutine Cancellation

**v2.0** introduces cooperative cancellation:

```c
int coco_cancel(coco_coro_t *coro);
int coco_cancelled(void);
```

Long-running coroutines should check `coco_cancelled()` periodically.

## New Features in v2.0

### Stack Pool

The stack pool reduces memory allocation overhead by reusing coroutine stacks:

- Stacks are returned to a pool when coroutines finish
- Configurable pool limits per stack size class
- Reduced memory fragmentation

### Stack Usage Telemetry

Monitor stack usage for optimization:

```c
size_t usage = coco_get_stack_usage(coro);
printf("Stack used: %zu bytes\n", usage);
```

### O(1) Timer Cancellation

Timers use a hierarchical timing wheel with bidirectional linked lists for O(1) cancellation:

- 4-level timing wheel for wide time range (0 - 2^32 ms)
- 1ms precision
- O(1) add and cancel operations

## Platform Support

**v2.0 Supported Platforms:**
- Linux (x86-64, ARM64) - epoll for I/O multiplexing
- macOS (x86-64, ARM64) - kqueue for I/O multiplexing

**Windows:** Planned for future release.

## Build Changes

No changes to build system. Continue using CMake:

```bash
mkdir build && cd build
cmake ..
make
```

## API Compatibility Matrix

| API | v1.x | v2.0 | Notes |
|-----|------|------|-------|
| `coco_sched_create` | Yes | Yes | Unchanged |
| `coco_create` | Yes | Yes | Unchanged |
| `coco_timer` | `void` | `coco_timer_t*` | Returns handle |
| `coco_timer_cancel` | - | Yes | New API |
| `coco_set_priority` | - | Yes | New API |
| `coco_cancel` | - | Yes | New API |
| `coco_cancelled` | - | Yes | New API |
| `coco_get_stack_usage` | - | Yes | New API |

## Testing Your Migration

1. Compile with `-DCOCO_DEPRECATED_WARNINGS` to identify deprecated usage
2. Run the test suite: `ctest --output-on-failure`
3. Monitor for any timing-related issues (timer cancellation changes)
4. Verify stack usage with `coco_get_stack_usage()` to tune stack sizes
