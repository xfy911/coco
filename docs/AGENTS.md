<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-06 | Updated: 2026-05-06 -->

# docs

## Purpose
Documentation for the coco coroutine library, including architecture design, assembly style guide, and migration notes.

## Key Files
| File | Description |
|------|-------------|
| `architecture.md` | Comprehensive architecture design document: module responsibilities, data flow, API contracts |
| `ASM_STYLE_GUIDE.md` | Assembly coding conventions for x86-64 and AArch64 context switching |
| `MIGRATION.md` | Migration guide for version upgrades and API changes |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- Architecture documentation should be updated when core module responsibilities change
- ASM style guide must be followed for all platform-specific assembly code
- Migration guides help users upgrade between versions

### Testing Requirements
- Documentation accuracy should be verified against actual implementation
- Code examples in documentation should be tested

### Common Patterns
- Architecture diagrams use ASCII art for compatibility
- Documentation is in Chinese (中文) to match the codebase comments

## Dependencies

### Internal
- `src/` — implementation code referenced in architecture docs
- `include/coco.h` — public API documented in architecture

### External
None.

<!-- MANUAL: Custom project notes can be added below -->
