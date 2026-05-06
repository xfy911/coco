<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-05-06 | Updated: 2026-05-06 -->

# coco_stack_pass

## Purpose
LLVM pass for stack map verification and analysis. Ensures coroutine stacks are properly formatted for garbage collection and debugging.

## Key Files
| File | Description |
|------|-------------|
| `CocoStackPass.cpp` | LLVM pass implementation: stack map analysis, statepoint verification |
| `post_process.py` | Post-processing script for pass output |
| `test_integration.sh` | Integration test script for the pass |
| `test_stack_map.c` | Test cases for stack map functionality |
| `verify_format.c` | Format verification utilities |

## Subdirectories
None.

## For AI Agents

### Working In This Directory
- This is a build tool, not part of the runtime library
- LLVM pass must be compiled against the same LLVM version as the project
- Stack maps enable precise garbage collection roots
- The pass runs as part of the build process for debug builds

### Testing Requirements
- Run `test_integration.sh` after building the pass
- Verify stack map format matches LLVM expectations
- Test with various coroutine configurations

### Common Patterns
- LLVM pass inherits from `llvm::FunctionPass` or `llvm::ModulePass`
- Statepoints mark safe points for garbage collection
- Stack maps record live variable locations

## Dependencies

### Internal
None.

### External
- LLVM (matching version required)
- Python 3.x for post-processing

<!-- MANUAL: Custom project notes can be added below -->
