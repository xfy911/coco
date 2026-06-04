# Release New Version

Publish a new version of coco.

## Prerequisites

- Working tree must be clean (no uncommitted changes)
- All CI checks should be passing on the current branch
- Confirm the version number with the user before proceeding

## Pre-release Checklist

If `[Unreleased]` in CHANGELOG.md is empty or incomplete, analyze commits since the last tag and update it first:

```bash
git log vLAST..HEAD --oneline   # review changes
git log vLAST..HEAD --reverse --format="%h %s%n%b"   # detailed review
```

Group changes into **Added / Changed / Fixed** (and **Removed** if applicable).

## Steps

1. **Update version strings** — Edit all 4 files:
   | File | Field to change |
   |------|----------------|
   | `CMakeLists.txt` L2 | `project(coco VERSION x.y.z ...)` |
   | `src/core/version.c` L3, L10 | version string and `coco_version_minor()` / `coco_version_patch()` |
   | `Doxyfile` L2 | `PROJECT_NUMBER = "x.y.z"` |
   | `AGENTS.md` L3 | `Version x.y.z` |

2. **Update CHANGELOG.md**:
   - Replace `## [Unreleased]` with `## [x.y.z] - YYYY-MM-DD`
   - Add a new empty `## [Unreleased]` section above it
   - Update the bottom comparison links:
     - Add `[x.y.z]: https://github.com/DefectingCat/coco/compare/vPREV...vx.y.z`
     - Change `[Unreleased]` link to compare against the new version

3. **Build and test**:
   ```bash
   cmake -B build && cmake --build build
   cd build && ctest --output-on-failure
   ```
   All tests must pass before proceeding.

4. **Commit and tag**:
   ```bash
   git add CMakeLists.txt src/core/version.c Doxyfile AGENTS.md CHANGELOG.md
   git commit -m "release x.y.z"
   git tag vx.y.z
   ```

5. **Push**:
   ```bash
   git push && git push --tags
   ```

6. **Create GitHub Release** — Extract the changelog section as release notes:
   ```bash
   gh release create vx.y.z --title "vx.y.z" --notes "$(sed -n '/^## \[x.y.z\]/,/^## \[/p' CHANGELOG.md | head -n -1 | tail -n +2)"
   ```

## Verification

- Confirm the tag exists: `git tag -l "vx.y.z"`
- Confirm the GitHub release URL is returned by `gh release create`
- If `gh` fails due to network, retry the command

## Version Numbering

Follow [SemVer](https://semver.org/):
- **Patch** (z++): Bug fixes only
- **Minor** (y++): New features, backward-compatible
- **Major** (x++): Breaking changes (ABI breaks, API removal)

> **Note:** Changes that require recompilation (e.g. struct layout, function signature changes) are breaking for C libraries and warrant at least a minor bump, or major if the API contract changes.
