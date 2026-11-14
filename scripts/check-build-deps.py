#!/usr/bin/env python3
"""Reject Ninja object files whose lost header dependencies make rebuilds unsafe."""
import pathlib
import re
import subprocess
import sys


def main():
    build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
    result = subprocess.run(
        # Without -n Ninja may open/compact shared logs even for a query tool.
        # Parallel probes must inspect the build without mutating those logs.
        ["ninja", "-n", "-C", str(build), "-t", "deps"],
        check=True, capture_output=True, text=True,
    )
    broken = []
    for line in result.stdout.splitlines():
        match = re.match(r"(.+\.o): #deps (\d+),", line)
        if match and int(match[2]) == 0 and (build / match[1]).exists():
            broken.append(match[1])
    if broken:
        print("Unsafe incremental build: object files have no header dependencies:", file=sys.stderr)
        print("\n".join(broken), file=sys.stderr)
        print("Run cmake --build <build-dir> --clean-first, then repeat this check.", file=sys.stderr)
        return 1
    print("Ninja dependency check passed: no existing objects with empty dependency records.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
