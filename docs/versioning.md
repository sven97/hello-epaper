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
