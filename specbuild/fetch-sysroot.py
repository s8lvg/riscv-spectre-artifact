#!/usr/bin/env python3
"""Fetch sysroot from remote host.
Usage:
    ./fetch-sysroot.py <host> <output_dir>
"""

import os
import subprocess
import sys
from pathlib import Path

SYSROOT_DIRS = "/usr/include /usr/lib /lib /lib64 /usr/lib64"


def fix_symlinks(sysroot: Path) -> int:
    """Convert absolute symlinks to relative ones."""
    sysroot = sysroot.resolve()
    count = 0
    for root, _, files in os.walk(sysroot):
        for f in files:
            fpath = Path(root) / f
            if fpath.is_symlink():
                link = os.readlink(fpath)
                if link.startswith("/") and not link.startswith(str(sysroot)):
                    new_link = os.path.relpath(str(sysroot) + link, root)
                    fpath.unlink()
                    fpath.symlink_to(new_link)
                    count += 1
    return count


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)

    host = sys.argv[1]
    output = Path(sys.argv[2])
    tarball = output.parent / f"{output.name}.tar.gz"

    # Use cached tarball if exists
    if tarball.exists() and output.exists():
        print(f"Already exists: {output}")
        return

    output.mkdir(parents=True, exist_ok=True)

    if tarball.exists():
        print(f"Extracting cached {tarball}...")
        subprocess.run(["tar", "xzf", str(tarball), "-C", str(output)], check=True)
        print(f"Done: {output}")
        return

    # Fetch from remote
    print(f"Fetching sysroot from {host} (compressing on target)...")
    tmp_tarball = tarball.with_suffix(".tmp.tar.gz")
    cmd = f"tar czf - {SYSROOT_DIRS} 2>/dev/null"
    with open(tmp_tarball, "wb") as f:
        subprocess.run(["ssh", host, cmd], stdout=f, check=True)

    print(f"Extracting...")
    subprocess.run(["tar", "xzf", str(tmp_tarball), "-C", str(output)], check=True)
    tmp_tarball.unlink()

    fixed = fix_symlinks(output)
    if fixed:
        print(f"Fixed {fixed} absolute symlinks")

    # Recompress with fixed symlinks
    print(f"Recompressing to {tarball}...")
    subprocess.run(["tar", "czf", str(tarball), "-C", str(output), "."], check=True)

    print(f"Done: {output}")


if __name__ == "__main__":
    main()
