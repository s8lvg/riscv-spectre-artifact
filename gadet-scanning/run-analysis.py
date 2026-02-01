#!/usr/bin/env python3
"""
Spectre gadget analysis for RISC-V
"""

import argparse
import os
import sys
import subprocess
from pathlib import Path
from datetime import datetime
from typing import Dict, Tuple

# RISC-V architecture configuration
ARCH_CONFIG = {
    'make_arch': 'riscv',
    'cross_compile': 'riscv64-linux-gnu-',
    'defconfig': 'defconfig',
}

VALID_TOOLS = ['smatch', 'codeql', 'all']


def get_ncpus() -> int:
    """Get number of CPU cores"""
    try:
        return int(subprocess.check_output(['nproc']).decode().strip())
    except:
        try:
            return int(subprocess.check_output(['sysctl', '-n', 'hw.ncpu']).decode().strip())
        except:
            return 4


def configure_kernel(kernel_dir: Path, arch_config: Dict[str, str], use_existing_config: bool = False) -> None:
    """Configure and clean kernel build"""
    env = os.environ.copy()
    env['ARCH'] = arch_config['make_arch']
    env['CROSS_COMPILE'] = arch_config['cross_compile']

    subprocess.run(
        ['make', 'clean', f'ARCH={arch_config["make_arch"]}'],
        cwd=kernel_dir,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    # Use existing .config if present and requested, otherwise use defconfig
    if use_existing_config and (kernel_dir / '.config').exists():
        print(f"Using existing .config from {kernel_dir}")
        subprocess.run(
            ['make', 'olddefconfig', f'ARCH={arch_config["make_arch"]}'],
            cwd=kernel_dir,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
    else:
        subprocess.run(
            ['make', arch_config['defconfig'], f'ARCH={arch_config["make_arch"]}'],
            cwd=kernel_dir,
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )


def run_smatch(
    script_dir: Path,
    kernel_dir: Path,
    arch_config: Dict[str, str],
    output_base: Path,
    ncpus: int
) -> Tuple[Path, int]:
    """Run Smatch analysis"""
    smatch_bin = script_dir / 'smatch' / 'smatch-src' / 'smatch'
    if not smatch_bin.exists():
        raise FileNotFoundError("Smatch not found. Run: cd smatch && ./setup.sh")

    print("=== Smatch ===")

    smatch_output = output_base / 'smatch'
    smatch_output.mkdir(parents=True, exist_ok=True)

    # Run Smatch build
    env = os.environ.copy()
    env['ARCH'] = arch_config['make_arch']
    env['CROSS_COMPILE'] = arch_config['cross_compile']

    with open(smatch_output / 'smatch_full.log', 'w') as log:
        subprocess.run(
            [
                'make', f'-j{ncpus}', 'C=2',
                f'CHECK={smatch_bin} --project=kernel',
                f'ARCH={arch_config["make_arch"]}',
                f'CROSS_COMPILE={arch_config["cross_compile"]}'
            ],
            cwd=kernel_dir,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )

    # Extract Spectre warnings
    spectre_warnings = smatch_output / 'spectre_warnings.txt'
    with open(smatch_output / 'smatch_full.log') as f:
        warnings = [
            line for line in f
            if 'potential spectre' in line.lower() or 'possible spectre' in line.lower()
        ]

    with open(spectre_warnings, 'w') as f:
        f.writelines(warnings)

    count = len(warnings)
    print(f"Found: {count} gadgets")
    print()

    return smatch_output, count


def run_codeql(
    script_dir: Path,
    kernel_dir: Path,
    arch_config: Dict[str, str],
    output_base: Path,
    ncpus: int,
    skip_db_build: bool = False,
    use_existing_config: bool = False
) -> Tuple[Path, int]:
    """Run CodeQL analysis"""
    codeql_bin = script_dir / 'codeql' / 'codeql-home' / 'codeql' / 'codeql'
    if not codeql_bin.exists():
        raise FileNotFoundError("CodeQL not found. Run: cd codeql && ./setup.sh")

    print("=== CodeQL ===")

    # Store CodeQL database in kernel version directory
    codeql_db_dir = output_base / 'codeql-db'
    codeql_db_dir.mkdir(parents=True, exist_ok=True)
    codeql_db = codeql_db_dir / 'db'

    # Store results in codeql/
    codeql_output = output_base / 'codeql'
    codeql_output.mkdir(parents=True, exist_ok=True)
    codeql_csv = codeql_output / 'spectre-v1-gadgets.csv'
    codeql_sarif = codeql_output / 'spectre-v1-gadgets.sarif'
    query_file = script_dir / 'codeql' / 'spectre-v1.ql'

    # Check if database exists or skip requested
    if codeql_db.exists() and not skip_db_build:
        print(f"Database exists, skipping rebuild (delete {codeql_db} to rebuild)")
    elif not skip_db_build:
        print("Creating database...")

        # Clean and reconfigure kernel
        configure_kernel(kernel_dir, arch_config, use_existing_config)

        # Create database
        result = subprocess.run(
            [
                str(codeql_bin), 'database', 'create', str(codeql_db),
                '--language=cpp',
                f'--command=make -j{ncpus} ARCH={arch_config["make_arch"]} '
                f'CROSS_COMPILE={arch_config["cross_compile"]} KCFLAGS=-Wno-error',
                '--overwrite'
            ],
            cwd=kernel_dir,
            capture_output=True,
            text=True
        )

        # Show only important lines
        for line in result.stderr.split('\n'):
            if any(keyword in line for keyword in ['Finalizing', 'Successfully created', 'ERROR']):
                print(line)

        # Explicitly finalize database (in case build failed and left it unfinalized)
        print("Finalizing database...")
        subprocess.run(
            [str(codeql_bin), 'database', 'finalize', str(codeql_db)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )

    print("Running query...")

    # Generate CSV for quick stats
    subprocess.run(
        [
            str(codeql_bin), 'database', 'analyze', str(codeql_db),
            str(query_file),
            '--format=csv',
            f'--output={codeql_csv}',
            '--threads=0'
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    # Generate SARIF for full path information
    subprocess.run(
        [
            str(codeql_bin), 'database', 'analyze', str(codeql_db),
            str(query_file),
            '--format=sarif-latest',
            f'--output={codeql_sarif}',
            '--threads=0'
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    # Count results
    count = 0
    if codeql_csv.exists():
        with open(codeql_csv) as f:
            count = sum(1 for _ in f) - 1  # Skip header
        print(f"Found: {count} gadgets")
    else:
        print("ERROR: Query failed, no output generated")

    print()

    return codeql_output, count



def parse_args():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description='Spectre gadget analysis for RISC-V',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s linux-6.6 ~/linux-riscv
  %(prog)s linux-6.6 ~/linux-riscv --tool smatch
  %(prog)s linux-6.6 ~/linux-riscv --tool codeql --skip-db-build
  %(prog)s th1520-5.10 ~/thead-kernel --use-existing-config
        """
    )

    parser.add_argument(
        'kernel_version',
        help='Kernel version label (e.g., linux-6.6)'
    )
    parser.add_argument(
        'kernel_path',
        type=Path,
        help='Path to kernel source directory'
    )
    parser.add_argument(
        '--tool',
        choices=VALID_TOOLS,
        default='all',
        help='Tool to run (default: all)'
    )
    parser.add_argument(
        '--skip-db-build',
        action='store_true',
        help='Skip CodeQL database build if it exists'
    )
    parser.add_argument(
        '--use-existing-config',
        action='store_true',
        help='Use existing .config file instead of defconfig (for custom kernel configs)'
    )

    return parser.parse_args()


def main():
    args = parse_args()

    # Validate kernel path
    if not args.kernel_path.exists():
        print(f"ERROR: Kernel directory not found: {args.kernel_path}")
        return 1

    script_dir = Path(__file__).parent.resolve()
    arch_config = ARCH_CONFIG

    # Create output directory structure: results/<kernel-version>/
    output_base = script_dir / 'results' / args.kernel_version
    output_base.mkdir(parents=True, exist_ok=True)

    # Create symlink to kernel source
    kernel_symlink = output_base / 'kernel'
    if not kernel_symlink.exists():
        kernel_symlink.symlink_to(args.kernel_path.resolve())

    print(f"Spectre Gadget Analysis (RISC-V)")
    print(f"Kernel version: {args.kernel_version}")
    print(f"Kernel path: {args.kernel_path}")
    print(f"Tool: {args.tool}")
    print(f"Use existing config: {args.use_existing_config}")
    print(f"Output: {output_base}/")
    print()

    # Configure kernel once
    configure_kernel(args.kernel_path, arch_config, args.use_existing_config)

    ncpus = get_ncpus()

    # Run requested tools
    results = {}

    try:
        if args.tool in ['smatch', 'all']:
            path, count = run_smatch(
                script_dir, args.kernel_path, arch_config, output_base, ncpus
            )
            results['smatch'] = {'path': path, 'count': count}

        if args.tool in ['codeql', 'all']:
            path, count = run_codeql(
                script_dir, args.kernel_path, arch_config,
                output_base, ncpus, args.skip_db_build, args.use_existing_config
            )
            results['codeql'] = {'path': path, 'count': count}

    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        return 1

    # Print results summary
    print("Results:")
    for tool, info in results.items():
        print(f"  {tool.capitalize()}: {info['count']} gadgets → {info['path']}/")
    print()
    print(f"All results: {output_base}/")

    return 0


if __name__ == '__main__':
    sys.exit(main())
