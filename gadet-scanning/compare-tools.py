#!/usr/bin/env python3
"""
Compare Smatch vs CodeQL results
"""

import argparse
import csv
import re
import sys
from pathlib import Path
from datetime import datetime
from typing import Set


def extract_location(line: str) -> str:
    """Extract file:line from Smatch warning"""
    match = re.match(r'^([^:]+):(\d+)', line)
    if match:
        return f"{match.group(1)}:{match.group(2)}"
    return None


def load_smatch_locations(warnings_file: Path) -> Set[str]:
    """Extract locations from Smatch warnings"""
    locations = set()
    if not warnings_file.exists():
        return locations

    with open(warnings_file) as f:
        for line in f:
            if 'potential spectre' in line.lower() or 'possible spectre' in line.lower():
                loc = extract_location(line)
                if loc:
                    locations.add(loc)
    return locations


def load_codeql_locations(csv_file: Path) -> Set[str]:
    """Extract locations from CodeQL CSV"""
    locations = set()
    if not csv_file.exists():
        return locations

    with open(csv_file) as f:
        reader = csv.reader(f)
        next(reader)  # Skip header
        for row in reader:
            if len(row) >= 6:
                file_path = row[4].lstrip('/')
                line_num = row[5]
                locations.add(f"{file_path}:{line_num}")
    return locations


def save_locations(locations: Set[str], output_file: Path) -> None:
    """Save sorted locations to file"""
    with open(output_file, 'w') as f:
        f.write('\n'.join(sorted(locations)) + '\n')


def parse_args():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description='Compare Smatch vs CodeQL results',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s linux-6.6
        """
    )

    parser.add_argument(
        'kernel_version',
        help='Kernel version label (e.g., linux-6.6)'
    )

    return parser.parse_args()


def main():
    args = parse_args()

    script_dir = Path(__file__).parent.resolve()
    results_base = script_dir / 'results' / args.kernel_version

    # Validate results exist
    if not results_base.exists():
        print(f"ERROR: No results found for {args.kernel_version}")
        print(f"Expected: {results_base}")
        print()
        print(f"Run analysis first:")
        print(f"  ./run-analysis.py {args.kernel_version} /path/to/kernel")
        return 1

    # Find Smatch and CodeQL results
    smatch_warnings = results_base / 'smatch' / 'spectre_warnings.txt'
    codeql_csv = results_base / 'codeql' / 'spectre-v1-gadgets.csv'

    if not smatch_warnings.exists():
        print(f"ERROR: Smatch results not found: {smatch_warnings}")
        return 1

    if not codeql_csv.exists():
        print(f"ERROR: CodeQL results not found: {codeql_csv}")
        return 1

    # Create comparison output directory
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    comparison_dir = results_base / 'comparison'
    comparison_dir.mkdir(parents=True, exist_ok=True)
    output_dir = comparison_dir / f'tool_comparison_{timestamp}'
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Tool Comparison (RISC-V)")
    print(f"Kernel: {args.kernel_version}")
    print()

    # Load locations
    smatch_locations = load_smatch_locations(smatch_warnings)
    codeql_locations = load_codeql_locations(codeql_csv)

    # Find overlaps
    both = smatch_locations & codeql_locations
    smatch_only = smatch_locations - codeql_locations
    codeql_only = codeql_locations - smatch_locations

    # Save results
    save_locations(smatch_locations, output_dir / 'smatch_locations.txt')
    save_locations(codeql_locations, output_dir / 'codeql_locations.txt')
    save_locations(both, output_dir / 'both_tools.txt')
    save_locations(smatch_only, output_dir / 'smatch_only.txt')
    save_locations(codeql_only, output_dir / 'codeql_only.txt')

    # Print summary
    overlap_pct = (len(both) / len(smatch_locations) * 100) if smatch_locations else 0

    print(f"Smatch:      {len(smatch_locations)} gadgets")
    print(f"CodeQL:      {len(codeql_locations)} gadgets")
    print(f"Both tools:  {len(both)} gadgets ({overlap_pct:.1f}% of Smatch)")
    print(f"Smatch only: {len(smatch_only)} gadgets")
    print(f"CodeQL only: {len(codeql_only)} gadgets")
    print()
    print(f"Results: {output_dir}/")

    return 0


if __name__ == '__main__':
    sys.exit(main())
