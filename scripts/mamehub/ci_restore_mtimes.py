#!/usr/bin/env python3
# Set each tracked file's mtime to its last git commit time so a restored
# object tree can be reused by make instead of rebuilding every source.

import os
import subprocess
import sys


def main():
    log = subprocess.check_output(
        ["git", "log", "--pretty=%ct", "--name-only", "--diff-filter=ACDMR"],
        text=True,
        errors="replace",
    )
    seen = set()
    commit_time = None
    updated = 0
    for line in log.splitlines():
        if not line:
            commit_time = None
            continue
        if commit_time is None and line.isdigit():
            commit_time = int(line)
            continue
        if commit_time is None or line in seen:
            continue
        seen.add(line)
        try:
            os.utime(line, (commit_time, commit_time))
            updated += 1
        except OSError:
            pass
    print(f"Restored mtimes for {updated} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
