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

## Build

```bash
cmake -B build
cmake --build build
```

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
```

## API Overview

### Scheduler

```c
coco_sched_t *coco_sched_create(void);
void coco_sched_destroy(coco_sched_t *sched);
int coco_sched_run(coco_sched_t *sched);
```

### Coroutine

```c
coco_coro_t *coco_create(coco_sched_t *sched, void (*entry)(void*), void *arg, size_t stack_size);
void coco_exit(coco_coro_t *coro, void *result);
void coco_yield(void);
void *coco_join(coco_coro_t *coro);
coco_coro_t *coco_self(void);
```

### Channel

```c
coco_channel_t *coco_channel_create(size_t capacity);
int coco_channel_send(coco_channel_t *ch, void *value);
int coco_channel_recv(coco_channel_t *ch, void **value);
void coco_channel_close(coco_channel_t *ch);
void coco_channel_destroy(coco_channel_t *ch);
```

### I/O

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
void coco_timer_cancel(coco_timer_t *timer);
```

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

```
include/coco.h           - Public API
src/coco_internal.h      - Internal structures
src/core/
  coro.c                 - Coroutine lifecycle
  stack.c                - Stack allocation + guard page
  signal.c               - Stack overflow detection
  context.c              - Context initialization
src/timer/
  timer_wheel.c          - 4-layer timing wheel
src/channel/
  channel.c              - Buffered/unbuffered channels
src/io/
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

MIT License