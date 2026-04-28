<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# io

## Purpose
Async I/O event loop and platform-specific I/O multiplexing. Integrates with the scheduler to block coroutines on I/O events and wake them when ready.

## Key Files
| File | Description |
|------|-------------|
| `event_loop.c` | Event loop: register/unregister file descriptors, process events, integrate with scheduler |
| `poll_linux.c` | Linux epoll-based I/O multiplexing implementation |
| `poll_macos.c` | macOS/BSD kqueue-based I/O multiplexing implementation |
| `poll_windows.c` | Windows IOCP-based I/O multiplexing implementation |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Platform-specific poll implementations are selected at compile time via `#ifdef`
- Each poll implementation must provide: `poll_create`, `poll_destroy`, `poll_add`, `poll_del`, `poll_wait`
- `event_loop.c` provides the platform-independent layer that calls into poll implementations
- When a coroutine waits on I/O, it's blocked and added to the poll set; on event, it's re-enqueued
- The event loop is called from the scheduler when the run queue is empty (idle poll)

### Testing Requirements
- Test I/O wait/wake-up with TCP sockets (echo server pattern)
- Verify edge cases: closed connections, partial reads, multiple concurrent waits
- Platform-specific tests should run on their respective OS

### Common Patterns
- `coco_io_register(fd, events, coro)` — block coroutine on fd events
- `coco_io_unregister(fd)` — remove fd from poll set
- Poll returns ready events; scheduler wakes corresponding coroutines
- One-shot mode: events must be re-registered after each trigger

## Dependencies

### Internal
- `src/coco_internal.h` — coroutine and scheduler structs
- `src/core/coro.c` — coroutine yield/resume

### External
- Linux: `sys/epoll.h`
- macOS: `sys/event.h` (kqueue)
- Windows: `winsock2.h`, `mswsock.h` (IOCP)

<!-- MANUAL: Custom project notes can be added below -->
