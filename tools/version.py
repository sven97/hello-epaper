# PlatformIO pre-build hook: stamps the firmware with the git commit it was
# built from (short hash, "-dirty" suffix if the tree has uncommitted
# changes). This is the only versioning scheme in the project — no manual
# semver to forget to bump; see docs/versioning.md.
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


env.Append(CPPDEFINES=[("FW_GIT_HASH", '\\"%s\\"' % git_hash())])
