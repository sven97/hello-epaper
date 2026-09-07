# PlatformIO pre-build hook: stamps the firmware with the git commit it was
# built from — FW_GIT_HASH (short hash, "-dirty" suffix if the tree has
# uncommitted changes) and FW_BUILD_NUMBER (commit count) as the
# auto-update ordering key. No manual semver to forget to bump; see
# docs/versioning.md.
Import("env")

import subprocess


def git_hash():
    try:
        h = subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except (subprocess.CalledProcessError, OSError):
        return "unknown"

    try:
        dirty = subprocess.call(
            ["git", "diff", "--quiet", "HEAD"],
            stderr=subprocess.DEVNULL,
        ) != 0
    except OSError:
        dirty = False

    return h + "-dirty" if dirty else h


def build_number():
    try:
        return subprocess.check_output(
            ["git", "rev-list", "--count", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except (subprocess.CalledProcessError, OSError):
        return "0"


env.Append(CPPDEFINES=[
    ("FW_GIT_HASH", '\\"%s\\"' % git_hash()),
    ("FW_BUILD_NUMBER", build_number()),  # bare int, unquoted — compared numerically on-device
])
