# Coco - A Production-Grade C Coroutine Library

A high-performance, cross-platform coroutine library written in C.

## Features

- **Stackful Coroutine**: 64KB fixed stack with guard page for overflow detection
- **Cooperative Scheduling**: Single-threaded event loop with run queue
- **Channel Communication**: Buffered and unbuffered channels (Go-style)
- **Async I/O**: epoll (Linux), kqueue (macOS), WSAPoll (Windows)
- **Timer**: 4-layer hierarchical timing wheel (1ms precision)
- **Cross-Platform**: Linux, macOS, Windows (x86-64 and ARM64)
- **High Performance**: Context switch < 100ns

## Thread Safety

Coco uses a **single-threaded cooperative model**. All coroutines run within one scheduler on one thread. The scheduler and its coroutines must not be accessed from multiple threads simultaneously.

## Platform Support

| Platform | Architecture | I/O Backend | Status |
|----------|-------------|-------------|--------|
| Linux | x86-64 | epoll | Supported |
| Linux | ARM64 | epoll | Supported |
| macOS | x86-64 | kqueue | Supported |
| macOS | ARM64 (Apple Silicon) | kqueue | Supported |
| Windows | x86-64 | WSAPoll | Stub |

## Build

```bash
cmake -B build
cmake --build build
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `COCO_BUILD_TESTS` | ON | Build unit tests and benchmarks |
| `COCO_BUILD_EXAMPLES` | ON | Build example programs |

## Run Tests

```bash
cd build
ctest --output-on-failure
```

## Run Benchmarks

```bash
cd build
./bench_switch    # Context switch benchmark
./bench_channel   # Channel throughput benchmark
./bench_io        # I/O throughput benchmark
```

## Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `COCO_OK` | 0 | Success |
| `COCO_ERROR` | -1 | Generic error |
| `COCO_ERROR_NOMEM` | -2 | Out of memory |
| `COCO_ERROR_STACK_OVERFLOW` | -3 | Stack overflow detected |
| `COCO_ERROR_CHANNEL_CLOSED` | -4 | Channel is closed |
| `COCO_ERROR_INVALID` | -5 | Invalid argument |

## Coroutine States

| State | Description |
|-------|-------------|
| `COCO_STATE_CREATED` | Coroutine created, not yet running |
| `COCO_STATE_RUNNING` | Coroutine is currently executing |
| `COCO_STATE_WAITING` | Coroutine waiting for I/O or channel |
| `COCO_STATE_READY` | Coroutine ready to be scheduled |
| `COCO_STATE_DEAD` | Coroutine has finished |
| `COCO_STATE_OVERFLOW` | Stack overflow (unrecoverable) |

## Default Configuration

| Constant | Value | Description |
|----------|-------|-------------|
| `COCO_DEFAULT_STACK_SIZE` | 64 KB | Default stack size per coroutine |
| `COCO_MAX_COROUTINES` | 10000 | Maximum concurrent coroutines |

## API Reference

### Scheduler

```c
coco_sched_t *coco_sched_create(void);
```
Create a scheduler. Returns NULL on failure.

```c
void coco_sched_destroy(coco_sched_t *sched);
```
Destroy a scheduler and free all resources.

```c
int coco_sched_run(coco_sched_t *sched);
```
Run the scheduler until no coroutines remain. Returns `COCO_OK` on success, negative error code on failure.

```c
int coco_sched_run_once(coco_sched_t *sched);
```
Execute a single scheduling step (process one coroutine). Returns `COCO_OK` on success, negative error code on failure.

### Coroutine Lifecycle

```c
coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size);
```
Create a coroutine. Pass `0` for `stack_size` to use `COCO_DEFAULT_STACK_SIZE`. Returns NULL on failure.

```c
void coco_exit(coco_coro_t *coro, void *result);
```
Exit the current coroutine with a result value. Must be called from within the coroutine.

```c
void coco_yield(void);
```
Yield execution to the scheduler. Must be called from within a coroutine.

```c
void *coco_join(coco_coro_t *coro);
```
Wait for a coroutine to finish and return its result. Blocks the calling coroutine.

```c
void coco_destroy(coco_coro_t *coro);
```
Destroy a coroutine and free its stack.

### Coroutine Query

```c
coco_coro_t *coco_self(void);
```
Return the currently running coroutine. Returns NULL if called from outside a coroutine.

```c
coco_state_t coco_get_state(coco_coro_t *coro);
```
Return the current state of a coroutine.

```c
uint64_t coco_get_id(coco_coro_t *coro);
```
Return the unique ID of a coroutine.

```c
void coco_set_error_cb(coco_coro_t *coro, coco_error_cb cb);
```
Set an error callback for a coroutine. The callback signature is:
```c
typedef void (*coco_error_cb)(coco_coro_t *coro, int error_code, const char *msg);
```

### Channel

```c
coco_channel_t *coco_channel_create(size_t capacity);
```
Create a channel. Pass `0` for unbuffered (synchronous). Returns NULL on failure.

```c
int coco_channel_send(coco_channel_t *ch, void *value);
```
Send a value through the channel. Blocks if the channel is full (buffered) or no receiver is ready (unbuffered). Returns `COCO_OK` on success, `COCO_ERROR_CHANNEL_CLOSED` if closed.

```c
int coco_channel_recv(coco_channel_t *ch, void **value);
```
Receive a value from the channel. Blocks if the channel is empty. Returns `COCO_OK` on success, `COCO_ERROR_CHANNEL_CLOSED` if closed and empty.

```c
void coco_channel_close(coco_channel_t *ch);
```
Close a channel. Pending receivers will get `COCO_ERROR_CHANNEL_CLOSED`.

```c
void coco_channel_destroy(coco_channel_t *ch);
```
Destroy a channel and free resources.

### I/O

All I/O operations block the calling coroutine and resume when the operation is ready.

```c
int coco_read(int fd, void *buf, size_t count);
int coco_write(int fd, const void *buf, size_t count);
int coco_accept(int fd, void *addr, size_t *addrlen);
int coco_connect(int fd, const void *addr, size_t addrlen);
int coco_sleep(uint64_t ms);
```

### Timer

```c
coco_timer_t *coco_timer(uint64_t delay_ms, void (*callback)(void*), void *arg);
```
Create a one-shot timer that fires after `delay_ms` milliseconds. Returns NULL on failure.

```c
void coco_timer_cancel(coco_timer_t *timer);
```
Cancel a pending timer.

## Examples

### Basic Coroutine

```c
#include "coco.h"
#include <stdio.h>

void coro_func(void *arg) {
    printf("Coroutine started\n");
    coco_yield();
    printf("Coroutine resumed\n");
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_create(sched, coro_func, NULL, 0);
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    return 0;
}
```

### Pipeline Pattern

See `examples/pipeline.c` for a producer-processor-consumer pattern using channels.

### Echo Server

See `examples/echo_server.c` for a high-concurrency TCP echo server.

## Architecture

See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.

```
include/coco.h           - Public API
src/coco_internal.h      - Internal structures
src/core/
  coro.c                 - Coroutine lifecycle
  sched.c                - Scheduler and run queue
  stack.c                - Stack allocation + guard page
  signal.c               - Stack overflow detection
  context.c              - Context initialization
src/timer/
  timer_wheel.c          - 4-layer timing wheel
src/channel/
  channel.c              - Buffered/unbuffered channels
src/io/
  event_loop.c           - Event loop integration
  poll_linux.c           - epoll event loop
  poll_macos.c           - kqueue event loop
  poll_windows.c         - WSAPoll event loop
src/platform/
  linux/ctx_x86_64.S     - x86-64 assembly (System V ABI)
  linux/ctx_arm64.S      - ARM64 assembly
  macos/ctx_x86_64.S     - macOS x86-64 assembly
  macos/ctx_arm64.S      - macOS ARM64 assembly (Apple Silicon)
  windows/ctx_x86_64.S   - Windows x86-64 assembly (Microsoft ABI)
```

## License

MIT License. See [LICENSE](LICENSE) for details.
