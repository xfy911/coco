<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-04-28 | Updated: 2026-04-28 -->

# include

## Purpose
Public API header for the coco coroutine library. This is the only header users need to include.

## Key Files
| File | Description |
|------|-------------|
| `coco.h` | Complete public API: coroutine lifecycle, scheduler, channel, I/O, timer, signal handling |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- This is the public interface; changes here affect all downstream code
- API functions are thin wrappers that delegate to internal implementations
- All public functions follow the `coco_*` naming convention
- Error codes defined as `coco_error_t` enum
- Coroutine states defined as `coco_state_t` enum

### Testing Requirements
- Any API change must be reflected in tests and examples
- Backward compatibility must be preserved

### Common Patterns
- Opaque handle types (`coco_t`, `coco_channel_t`, `coco_sched_t`) returned by value
- Configuration via struct parameters (`coco_config_t`, `coco_channel_config_t`)
- Timeout parameters use `coco_duration_t` (milliseconds)

## Dependencies

### Internal
- `src/core/` — coroutine and scheduler implementation
- `src/channel/` — channel implementation
- `src/io/` — I/O multiplexing
- `src/timer/` — timer wheel

### External
- C11 standard library (for `stdalign.h`, `stdbool.h`, `stdint.h`)

<!-- MANUAL: Custom project notes can be added below -->
