<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-06 | Updated: 2026-05-06 -->

# tools

## Purpose
Build tools and utilities for the coco coroutine library. Contains development-time tools for analysis, verification, and debugging.

## Key Files
None.

## Subdirectories
| Directory | Purpose |
|-----------|---------|
| `coco_stack_pass/` | LLVM pass for stack map verification (see `coco_stack_pass/AGENTS.md`) |

## For AI Agents

### Working In This Directory
- Tools in this directory are not part of the runtime library
- They are used during development, testing, or build process
- Some tools may require specific dependencies (e.g., LLVM)

### Testing Requirements
- Each tool has its own test suite
- Tools should be tested independently from the main library

### Common Patterns
- Tools are optional dependencies for the main build
- They may have different build requirements than the library

## Dependencies

### Internal
None.

### External
Varies by tool (see individual tool documentation).

<!-- MANUAL: Custom project notes can be added below -->
