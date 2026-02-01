#!/usr/bin/env python3
"""
Sync analysis results and kernel sources from remote machine
"""

import argparse
import sys
import subprocess
from pathlib import Path


KERNEL_EXCLUDES = [
    '.git/', '*.o', '*.ko', '*.a',
    '.tmp_*', '*.cmd', '*.mod', '*.mod.c',
    'vmlinux*', 'System.map', 'Module.symvers', 'modules.order',
    'Documentation/', 'samples/', 'tools/',
    'scripts/gcc-plugins/', 'usr/',
    '*.rst', '*.txt'
]


def run_rsync(source: str, dest: Path, excludes: list = None, show_progress: bool = False, follow_symlinks: bool = False) -> bool:
    """Run rsync with common options"""
    cmd = ['rsync', '-az']

    if follow_symlinks:
        cmd.append('-L')  # Follow symlinks and copy the files they point to

    if show_progress:
        cmd.append('--info=progress2')

    if excludes:
        for pattern in excludes:
            cmd.extend(['--exclude', pattern])

    cmd.extend([source, str(dest)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode == 0


def parse_args():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description='Sync analysis results and kernel source from remote machine',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Downloads:
  - All analysis results (Smatch, CodeQL)
  - CodeQL databases
  - Comparison results
  - Kernel source (excluding .git and build artifacts)

Example:
  %(prog)s linux-6.6
  %(prog)s linux-6.6 --remote lab77
        """
    )

    parser.add_argument(
        'kernel_version',
        help='Kernel version label (e.g., linux-6.6)'
    )
    parser.add_argument(
        '--remote',
        default='lab25',
        help='Remote hostname (default: lab25)'
    )

    return parser.parse_args()


def main():
    args = parse_args()

    local_dir = Path(__file__).parent.resolve()
    local_results = local_dir / 'results' / args.kernel_version

    print(f"=== Syncing {args.kernel_version} from {args.remote} ===")
    print()

    # Create local directory structure
    local_results.mkdir(parents=True, exist_ok=True)

    remote_results = f"~/2024_riscv_speculate/experiments/spectre-gadget-scan/results/{args.kernel_version}"

    # Sync entire kernel version directory
    print("1. Syncing analysis results...")
    success = run_rsync(
        f"{args.remote}:{remote_results}/",
        local_results,
        excludes=['kernel/', 'codeql-db/']  # Don't sync kernel/db yet, do separately
    )
    if not success:
        print(f"  Warning: Could not sync results from {args.remote}:{remote_results}")

    # Sync kernel source separately
    print("2. Syncing kernel source (compressing for faster transfer)...")

    kernel_dest = local_results / 'kernel'
    success = run_rsync(
        f"{args.remote}:{remote_results}/kernel/",
        kernel_dest,
        excludes=KERNEL_EXCLUDES,
        show_progress=True,
        follow_symlinks=True  # Follow symlink to actual kernel source
    )
    if not success:
        print("  Warning: Kernel source not found")
    print()

    print("=== Sync Complete ===")
    print()
    print("Downloaded to:")
    print(f"  All results: {local_results}/")
    print()

    # Show next steps
    print("To compare tools (Smatch vs CodeQL):")
    print(f"  ./compare-tools.py {args.kernel_version}")

    return 0


if __name__ == '__main__':
    sys.exit(main())
