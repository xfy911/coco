# Coco v2.0 Migration Guide

This guide documents breaking changes when upgrading from v1.x to v2.0 and introduces new Go-like runtime features.

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

### 1. Dynamic Stack Management (Phase 1)

Coco now supports dynamic stack growth for coroutines that need more than the default 64KB stack.

#### Opt-in Dynamic Stack

To enable dynamic stack growth, create a coroutine with a small initial stack size:

```c
// Create coroutine with 2KB initial stack (dynamic stack enabled)
coco_coro_t *coro = coco_create(sched, my_entry, arg, 2048);
```

**Key points:**
- Default stack size remains 64KB (no change for existing code)
- Stack sizes < 64KB enable dynamic growth
- Maximum stack size: 8MB (`COCO_STACK_MAX_SIZE`)
- Requires stack map for pointer adjustment during growth

#### Stack Map Requirement

Dynamic stack growth requires a stack map file for pointer adjustment:

```c
// Set stack map path before creating dynamic stack coroutines
setenv("COCO_STACKMAP_PATH", "/path/to/stack.map", 1);

// Or validate programmatically
if (coco_validate_stack_map(sched) != COCO_OK) {
    fprintf(stderr, "Stack map not loaded, dynamic stack may fail\n");
}
```

#### Stack Growth Behavior

When a coroutine's stack approaches capacity:
1. A new larger stack is allocated (2x current size)
2. Stack contents are copied
3. Frame pointers are adjusted
4. Stack map pointers are updated

```c
// Check if stack growth is needed
if (coro->stack_growable && /* stack nearly full */) {
    // Runtime automatically grows the stack
}
```

### 2. Fair Scheduling (Phase 2)

Time-slice based fair scheduling prevents CPU-bound coroutines from starving others.

#### Enabling Fair Scheduling

```c
coco_sched_t *sched = coco_sched_create();

// Enable fair scheduling with 10ms time slice
coco_sched_set_fairness(sched, true, 10);

// Or with custom time slice (5ms)
coco_sched_set_fairness(sched, true, 5);

// Disable fair scheduling
coco_sched_set_fairness(sched, false, 0);
```

**Default:** Fair scheduling is disabled by default for backward compatibility.

#### Time Slice Behavior

When enabled:
- Each coroutine gets at most `time_slice_ms` milliseconds per turn
- CPU-bound coroutines are automatically preempted
- Other coroutines get fair scheduling opportunities

### 3. Asynchronous Preemption (Phase 3)

Signal-based preemption ensures long-running coroutines don't block the scheduler.

#### Preemption API

```c
// Enable preemption for current coroutine
coco_preempt_enable();

// Disable preemption (for critical sections)
coco_preempt_disable();

// Check if preemption is pending
if (coco_preempt_is_pending()) {
    // Preemption will occur soon
}

// Cooperative preemption checkpoint
coco_preempt_checkpoint();  // Yields if preemption pending
```

#### Usage Pattern

```c
void cpu_intensive_coroutine(void *arg) {
    coco_preempt_enable();  // Enable preemption

    for (int i = 0; i < BIG_NUMBER; i++) {
        do_work();

        // Periodic checkpoint allows preemption
        if (i % 100 == 0) {
            coco_preempt_checkpoint();
        }
    }
}
```

### 4. Stack Pool

The stack pool reduces memory allocation overhead by reusing coroutine stacks:

- Stacks are returned to a pool when coroutines finish
- Configurable pool limits per stack size class
- Reduced memory fragmentation

### 5. Stack Usage Telemetry

Monitor stack usage for optimization:

```c
size_t usage = coco_get_stack_usage(coro);
printf("Stack used: %zu bytes\n", usage);
```

### 6. Multi-threaded Scheduler

Launch coroutines across multiple threads for parallelism:

```c
// Start global scheduler with auto-detected thread count
coco_global_sched_start(0);  // 0 = auto-detect

// Or specify thread count
coco_global_sched_start(4);

// Launch coroutine (auto-distributed to workers)
coco_coro_t *coro = coco_go(my_entry, arg);

// Launch with options
coco_go_opts_t opts = {
    .stack_size = 8192,      // Custom stack size
    .priority = COCO_PRIORITY_HIGH,
    .p_id = -1               // Auto-select P
};
coco_go_with_opts(my_entry, arg, &opts);

// Wait for all coroutines to complete
coco_global_sched_wait();

// Stop scheduler
coco_global_sched_stop();
```

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Context switch | < 100ns | Baseline performance |
| Preemption latency p99 | <= 15ms | With fair scheduling enabled |
| Stack growth overhead | < 1μs | Stack pool allocation |
| Checkpoint overhead | < 10ns | Per checkpoint call |

## API Compatibility Matrix

| API | v1.x | v2.0 | Notes |
|-----|------|------|-------|
| `coco_sched_create` | Yes | Yes | Unchanged |
| `coco_create` | Yes | Yes | Now supports dynamic stack |
| `coco_timer` | `void` | `coco_timer_t*` | Returns handle |
| `coco_timer_cancel` | - | Yes | New API |
| `coco_set_priority` | - | Yes | New API |
| `coco_cancel` | - | Yes | New API |
| `coco_cancelled` | - | Yes | New API |
| `coco_get_stack_usage` | - | Yes | New API |
| `coco_sched_set_fairness` | - | Yes | New API |
| `coco_preempt_enable` | - | Yes | New API |
| `coco_preempt_disable` | - | Yes | New API |
| `coco_preempt_checkpoint` | - | Yes | New API |
| `coco_validate_stack_map` | - | Yes | New API |
| `coco_global_sched_start` | - | Yes | New API |
| `coco_go` | - | Yes | New API |

## Migration Checklist

- [ ] Update timer calls to handle return value (optional for cancellation)
- [ ] Review multi-scheduler code for per-scheduler FD table changes
- [ ] Consider enabling fair scheduling for CPU-bound workloads
- [ ] Add `coco_cancelled()` checks to long-running coroutines
- [ ] Use `coco_preempt_checkpoint()` in CPU-intensive loops
- [ ] Consider dynamic stack for deep recursion use cases
- [ ] Test with new benchmark suite (`bench_preempt`, `bench_stack`)

## Testing Your Migration

1. Compile with `-DCOCO_DEPRECATED_WARNINGS` to identify deprecated usage
2. Run the test suite: `ctest --output-on-failure`
3. Run new benchmark tests:
   ```bash
   ./build/bench_preempt  # Preemption latency
   ./build/bench_stack    # Stack growth overhead
   ```
4. Monitor for any timing-related issues (timer cancellation changes)
5. Verify stack usage with `coco_get_stack_usage()` to tune stack sizes

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

For benchmarks:

```bash
cmake -DCOCO_BUILD_TESTS=ON -DCOCO_BUILD_EXAMPLES=ON ..
make
```

## Troubleshooting

### Dynamic Stack Not Growing

**Symptom:** Coroutine crashes with stack overflow despite small initial stack.

**Solution:** Ensure stack map is loaded:
```c
if (coco_validate_stack_map(sched) != COCO_OK) {
    fprintf(stderr, "Set COCO_STACKMAP_PATH environment variable\n");
}
```

### Preemption Not Working

**Symptom:** Long-running coroutines block the scheduler.

**Solution:** 
1. Enable fair scheduling: `coco_sched_set_fairness(sched, true, 10)`
2. Add checkpoints: `coco_preempt_checkpoint()`
3. Enable preemption: `coco_preempt_enable()`

### Performance Regression

**Symptom:** Lower throughput with fair scheduling.

**Solution:** Fair scheduling adds overhead. Disable for I/O-bound workloads:
```c
coco_sched_set_fairness(sched, false, 0);
```
