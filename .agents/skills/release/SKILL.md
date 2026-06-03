# Release New Version

Publish a new version of coco.

## Prerequisites

- Working tree must be clean (no uncommitted changes)
- All CI checks should be passing on the current branch
- Confirm the version number with the user before proceeding

## Steps

1. **Update version** — Edit `CMakeLists.txt` line 2:
   ```
   project(coco VERSION x.y.z LANGUAGES C ASM)
   ```

2. **Update CHANGELOG.md**:
   - Replace `## [Unreleased]` with `## [x.y.z] - YYYY-MM-DD`
   - Add a new `## [Unreleased]` section above it (empty, just the heading)
   - Update the bottom comparison links:
     - Add `[x.y.z]: https://github.com/DefectingCat/coco/compare/vPREV...vx.y.z`
     - Change `[Unreleased]` link to compare against the new version

3. **Commit and tag**:
   ```bash
   git add CMakeLists.txt CHANGELOG.md
   git commit -m "release x.y.z"
   git tag vx.y.z
   ```

4. **Push**:
   ```bash
   git push && git push --tags
   ```

5. **Create GitHub Release** — Extract the changelog section as release notes:
   ```bash
   gh release create vx.y.z --title "vx.y.z" --notes "$(sed -n '/^## \[x.y.z\]/,/^## \[/p' CHANGELOG.md | head -n -1 | tail -n +2)"
   ```

## Verification

- Confirm the tag exists: `git tag -l "vx.y.z"`
- Confirm the GitHub release URL is returned by `gh release create`
- If `gh` fails due to network, retry the command
