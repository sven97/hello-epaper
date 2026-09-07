# Firmware versioning

There's no manual version number (no semver, no CHANGELOG, no git tags) —
one dev, no release process to serve, and a hand-bumped number is just
another thing to forget.

Instead, every build is stamped with the git commit it came from:
`tools/version.py` runs as a PlatformIO pre-build hook and injects
`FW_GIT_HASH` (an 8-char short hash, e.g. `a1b2c3d`, or `a1b2c3d-dirty` if
the working tree had uncommitted changes at build time) as a compile-time
`-D` define, shared by all board envs via `xiao_base` in `platformio.ini`.

It shows up in two places on the running board:

- the first line of the boot log (`/log`, `/debug`)
- the header of the `/debug` page

To find out what's actually flashed on a board, hit `/log` or `/debug` and
read the hash — then `git show <hash>` locally to see what it was built
from. If a build ever runs outside a git checkout (e.g. a source tarball),
`FW_GIT_HASH` falls back to `"unknown"` rather than failing the build.

## `FW_BUILD_NUMBER` — the auto-update ordering key

The git hash identifies *which* commit a build came from but can't answer
"is this newer than what I'm running" — hashes don't order. The on-device
auto-update path (see
`docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md`) needs
that answer, so `tools/version.py` also stamps `FW_BUILD_NUMBER`:
`git rev-list --count HEAD`, the commit count reachable from the current
commit. Monotonic on a linear `main`. Outside a git checkout it falls
back to `0`, which the firmware treats as "never auto-update" — a build
with no provenance can't reason about "newer". Like the hash, it is never
hand-edited; CI and the local build derive it the same way.
