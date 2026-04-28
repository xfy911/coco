<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# channel

## Purpose
Go-style channel communication for coroutines. Supports buffered and unbuffered channels, select/multiplexing, and blocking send/recv with coroutine wake-up.

## Key Files
| File | Description |
|------|-------------|
| `channel.c` | Channel implementation: create, send, recv, close, select; wait queue management; coroutine blocking/wake-up |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Channels are the primary synchronization primitive between coroutines
- Unbuffered channels synchronize sender and receiver (rendezvous)
- Buffered channels use a ring buffer with configurable capacity
- `channel.c` is the second most complex file after `coro.c` — use-after-free bugs are a concern
- When a coroutine blocks on channel send/recv, it's added to the channel's wait queue and yielded
- On wake-up, the coroutine is re-enqueued to the scheduler's run queue

### Testing Requirements
- Test both buffered and unbuffered channels
- Test channel close semantics (pending receivers get zero/EOF)
- Test select with multiple channels
- Verify no use-after-free when coroutines are cancelled while blocked on a channel

### Common Patterns
- Wait queues: linked list of `coco_channel_waiter_t` nodes
- Ring buffer for buffered channels: `head`, `tail`, `count` indices
- `coco_channel_select()` iterates channels in order; first ready channel wins
- Channel close sets a flag; subsequent send returns error, recv drains remaining items

## Dependencies

### Internal
- `src/coco_internal.h` — coroutine struct and scheduler access
- `src/core/coro.c` — coroutine yield/resume for blocking operations

### External
- C11 standard library (stdlib, string)

<!-- MANUAL: Custom project notes can be added below -->
