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

| Platform | Architecture | ABI | I/O Backend | Status |
|----------|-------------|-----|-------------|--------|
| Linux | x86-64 | System V AMD64 | epoll | Supported |
| Linux | ARM64 | AAPCS64 | epoll | Supported |
| macOS | x86-64 | System V AMD64 | kqueue | Supported |
| macOS | ARM64 (Apple Silicon) | AAPCS64 | kqueue | Supported |
| Windows | x86-64 | Microsoft x64 | WSAPoll | Supported |
| Windows | ARM64 | AAPCS64 | WSAPoll | Supported |

### ABI Compliance

All platform implementations follow their respective calling conventions:

- **System V AMD64** (Linux/macOS x86-64): Saves rbx, rbp, r12-r15
- **Microsoft x64** (Windows x86-64): Saves rbx, rbp, rsi, rdi, r12-r15, xmm6-xmm15
- **AAPCS64** (ARM64): Saves x19-x28, fp, lr, d8-d15

## Quick Start

### Build

```bash
cmake -B build
cmake --build build
```

This produces static library `build/libcoco.a`.

### Integrate Into Your Project

**Option 1: Link Static Library**

```bash
# Copy headers and library
cp include/coco.h your_project/include/
cp build/libcoco.a your_project/lib/

# Compile
gcc -Iyour_project/include your_code.c -Lyour_project/lib -lcoco -o your_program
```

**Option 2: CMake Subdirectory**

```cmake
add_subdirectory(coco)
target_link_libraries(your_target PRIVATE coco)
```

**Option 3: Direct Source Inclusion**

```bash
cp -r include/coco.h your_project/include/
cp -r src your_project/coco_src/
```

## Usage Examples

### Basic Coroutine

```c
#include "coco.h"
#include <stdio.h>

void my_coroutine(void *arg) {
    int *data = (int*)arg;
    printf("Coroutine received: %d\n", *data);

    coco_yield();  // Yield execution

    printf("Coroutine resumed\n");
}

int main(void) {
    // 1. Create scheduler
    coco_sched_t *sched = coco_sched_create();

    // 2. Create coroutine
    int value = 42;
    coco_create(sched, my_coroutine, &value, 0);

    // 3. Run scheduler
    coco_sched_run(sched);

    // 4. Cleanup
    coco_sched_destroy(sched);
    return 0;
}
```

### Channel Communication (Go-style)

```c
#include "coco.h"
#include <stdio.h>

void producer(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    for (int i = 0; i < 10; i++) {
        coco_channel_send(ch, (void*)(intptr_t)i);
        printf("Sent: %d\n", i);
    }
    coco_channel_close(ch);
}

void consumer(void *arg) {
    coco_channel_t *ch = (coco_channel_t*)arg;
    void *val;
    while (coco_channel_recv(ch, &val) == COCO_OK) {
        printf("Received: %ld\n", (long)val);
    }
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_channel_t *ch = coco_channel_create(5);  // Buffer size 5

    coco_create(sched, producer, ch, 0);
    coco_create(sched, consumer, ch, 0);

    coco_sched_run(sched);
    coco_channel_destroy(ch);
    coco_sched_destroy(sched);
    return 0;
}
```

### Async I/O

```c
#include "coco.h"
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

void handle_client(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buf[1024];

    ssize_t n = coco_read(fd, buf, sizeof(buf));  // Non-blocking wait
    if (n > 0) {
        coco_write(fd, buf, n);  // Echo back
    }
    close(fd);
}

void echo_server(void *arg) {
    int port = *(int*)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 128);

    printf("Listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        size_t addrlen = sizeof(client_addr);
        int client_fd = coco_accept(server_fd, &client_addr, &addrlen);
        if (client_fd >= 0) {
            coco_create(coco_self()->sched, handle_client,
                       (void*)(intptr_t)client_fd, 0);
        }
    }
}
```

### Timer

```c
#include "coco.h"
#include <stdio.h>

void timer_callback(void *arg) {
    printf("Timer fired: %s\n", (char*)arg);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();

    // Create timer that fires after 1000ms
    coco_timer_t *timer = coco_timer(1000, timer_callback, "Hello!");

    // Timer can be cancelled: coco_timer_cancel(timer);

    coco_sched_run(sched);
    coco_sched_destroy(sched);
    return 0;
}
```

### Coroutine Join (Wait for Result)

```c
#include "coco.h"
#include <stdio.h>

void worker(void *arg) {
    int input = *(int*)arg;
    int *result = malloc(sizeof(int));
    *result = input * 2;
    coco_exit(coco_self(), result);
}

void main_coro(void *arg) {
    int value = 21;
    coco_coro_t *worker_coro = coco_create(coco_self()->sched,
                                           worker, &value, 0);

    void *result = coco_join(worker_coro);  // Wait for completion
    printf("Result: %d\n", *(int*)result);
    free(result);
    coco_destroy(worker_coro);
}

int main(void) {
    coco_sched_t *sched = coco_sched_create();
    coco_create(sched, main_coro, NULL, 0);
    coco_sched_run(sched);
    coco_sched_destroy(sched);
    return 0;
}
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

### Channel Select

```c
int coco_channel_select(coco_select_case_t *cases, int ncases,
                        uint64_t timeout_ms, int has_default);
```
Select over multiple channel operations (Go-style). Blocks until one case is ready, timeout expires, or default is taken. Returns the index of the ready case, `COCO_SELECT_TIMEOUT`, or `COCO_SELECT_DEFAULT`.

```c
typedef struct coco_select_case {
    coco_channel_t *chan;        /* Channel */
    enum coco_select_dir dir;    /* COCO_SELECT_SEND or COCO_SELECT_RECV */
    void *val;                   /* Send value or recv output pointer */
    int result;                  /* Result: COCO_OK or error */
} coco_select_case_t;
```

**Example:**

```c
void *val1, *val2;
coco_select_case_t cases[2] = {
    { ch1, COCO_SELECT_RECV, &val1, 0 },
    { ch2, COCO_SELECT_RECV, &val2, 0 },
};
int idx = coco_channel_select(cases, 2, 1000, 0);
if (idx >= 0) {
    printf("Case %d ready, result=%d\n", idx, cases[idx].result);
} else if (idx == COCO_SELECT_TIMEOUT) {
    printf("Timeout\n");
}
```

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

## More Examples

See the `examples/` directory:

- `examples/pipeline.c` - Producer-processor-consumer pattern using channels
- `examples/echo_server.c` - High-concurrency TCP echo server

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
  linux/ctx_arm64.S      - ARM64 assembly (AAPCS64)
  macos/ctx_x86_64.S     - macOS x86-64 assembly (System V ABI)
  macos/ctx_arm64.S      - macOS ARM64 assembly (AAPCS64)
  windows/ctx_x86_64.S   - Windows x86-64 assembly (Microsoft x64 ABI)
  windows/ctx_arm64.S    - Windows ARM64 assembly (AAPCS64)
```

## License

MIT License. See [LICENSE](LICENSE) for details.
