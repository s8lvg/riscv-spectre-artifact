#!/usr/bin/env python3
"""
Cross-architecture Spectre mitigation comparison.

Compares known x86 mitigation sites against RISC-V to find equivalent functions.
"""

import argparse
import csv
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from mitigation_diffing.matching.embedding_matcher import EmbeddingMatcher
from mitigation_diffing.utils.colors import Colors


def load_mitigations(file_path: Path) -> List[Tuple[str, str, str]]:
    """Load mitigations from CSV file."""
    mitigations = []
    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(',')
            if len(parts) >= 3:
                mitigations.append((parts[0].strip(), parts[1].strip(), parts[2].strip()))
    return mitigations


class MitigationAnalyzer:
    """Compare x86 mitigations against RISC-V using embeddings."""

    # Patterns that indicate a Spectre mitigation is present
    MITIGATION_PATTERNS = [
        # Macros
        'array_index_nospec', 'array_index_mask_nospec',
        'barrier_nospec', 'nospec_ptr', 'nospec_array_ptr',
        # x86 fences
        'lfence', 'mfence',
        # ARM barriers
        'csdb', 'dsb', 'isb',
        # RISC-V
        'fence.i', 'fence ',
        # Generic
        'speculation', 'spectre', 'NOSPEC',
        # uaccess
        'uaccess_mask_ptr', '__uaccess_begin_nospec',
    ]

    def __init__(self, kernel_path: str, mitigations: List[Tuple[str, str, str]], top_k: int = 3):
        self.kernel_path = Path(kernel_path)
        self.mitigations = mitigations
        self.top_k = top_k
        self.matcher = EmbeddingMatcher()

    def has_mitigation(self, text: str) -> List[str]:
        """Check if code contains mitigation patterns. Returns list of found patterns."""
        if not text:
            return []
        found = []
        text_lower = text.lower()
        for pattern in self.MITIGATION_PATTERNS:
            if pattern.lower() in text_lower:
                found.append(pattern)
        return found

    def find_file(self, suffix: str) -> Optional[Path]:
        """Find file in kernel by suffix."""
        full_path = self.kernel_path / suffix
        if full_path.exists():
            return full_path
        return None

    def cache_riscv(self):
        """Build RISC-V embedding cache."""
        if self.matcher.has_cached('riscv'):
            load_start = time.time()
            self.matcher.load_cache('riscv')
            load_elapsed = time.time() - load_start
            funcs = self.matcher.get_cached_functions('riscv')
            print(f"Loaded {len(funcs)} cached RISC-V embeddings in {load_elapsed:.1f}s")
            return

        print(f"Building RISC-V embedding cache (one-time)...")

        # Generate ctags for riscv
        riscv_path = self.kernel_path / 'arch' / 'riscv'
        if not riscv_path.exists():
            print(f"{Colors.RED}Error: {riscv_path} not found{Colors.RESET}")
            return

        tags_file = Path('riscv_tags')
        subprocess.run([
            'ctags', '-R', '--fields=+nK', '--c-kinds=+p',
            '-f', str(tags_file), str(riscv_path)
        ], capture_output=True)

        # Parse tags
        funcs = []
        with open(tags_file, 'r') as f:
            for line in f:
                if line.startswith('!'):
                    continue
                parts = line.strip().split('\t')
                if len(parts) >= 4 and 'f' in parts[3]:
                    line_num = None
                    for p in parts:
                        if p.startswith('line:'):
                            line_num = int(p.split(':')[1])
                            break
                    if line_num:
                        funcs.append({
                            'name': parts[0],
                            'file': parts[1],
                            'line': line_num
                        })

        start = time.time()
        count = self.matcher.cache_functions('riscv', funcs, batch_size=32)
        elapsed = time.time() - start
        print(f"Cached {count} RISC-V functions in {elapsed:.1f}s")

    def find_function_line(self, file_path: Path, func_name: str) -> Optional[int]:
        """Find line number of function in file."""
        try:
            with open(file_path, 'r') as f:
                for i, line in enumerate(f, 1):
                    # Simple heuristic: function definition
                    if func_name in line and '(' in line and '{' in line:
                        return i
                    if func_name in line and '(' in line:
                        # Check next line for {
                        return i
        except:
            pass
        return None

    def analyze(self):
        """Run analysis."""
        # Cache RISC-V embeddings (silent if already cached)
        self.cache_riscv()

        results = []
        skipped = 0
        missing_mitigation = 0

        search_start = time.time()
        for file_suffix, func_name, mitigation_type in self.mitigations:
            file_path = self.find_file(file_suffix)
            if not file_path:
                skipped += 1
                continue

            line_num = self.find_function_line(file_path, func_name)
            if not line_num:
                skipped += 1
                continue

            # Extract source function text
            src_text = self.matcher.extract_function_text(str(file_path), line_num)
            if not src_text:
                skipped += 1
                continue

            # Check if source has mitigation
            src_mitigations = self.has_mitigation(src_text)
            if not src_mitigations:
                skipped += 1
                continue

            # Find RISC-V matches
            matches = self.matcher.rank_against_cached(src_text, 'riscv', top_k=self.top_k)

            # Print source function with detected mitigations
            print(f"{Colors.BOLD}{func_name}{Colors.RESET}")
            print(f"  src: {file_path}:{line_num}")
            print(f"  {Colors.CYAN}mitigations: {', '.join(src_mitigations)}{Colors.RESET}")

            if not matches:
                print(f"  {Colors.RED}No RISC-V match found{Colors.RESET}\n")
                results.append({'src': func_name, 'dst': None, 'score': 0, 'status': 'no_match'})
                continue

            # Check best match for mitigations
            best_match, best_score = matches[0]
            riscv_text = self.matcher.extract_function_text(best_match['file'], best_match['line'])
            riscv_mitigations = self.has_mitigation(riscv_text)

            # Show match with status
            if riscv_mitigations:
                status = f"{Colors.GREEN}[OK]{Colors.RESET}"
                status_key = 'mitigated'
            else:
                status = f"{Colors.RED}[MISSING]{Colors.RESET}"
                status_key = 'missing'
                missing_mitigation += 1

            print(f"  {status} {best_match['name']} [{best_score:.3f}]")
            print(f"       {best_match['file']}:{best_match['line']}")
            if riscv_mitigations:
                print(f"       {Colors.CYAN}mitigations: {', '.join(riscv_mitigations)}{Colors.RESET}")

            results.append({
                'src': func_name,
                'dst': best_match['name'],
                'score': best_score,
                'status': status_key,
                'src_mitigations': src_mitigations,
                'dst_mitigations': riscv_mitigations
            })
            print()

        # Timing
        search_elapsed = time.time() - search_start
        print(f"Search completed in {search_elapsed:.1f}s ({len(self.mitigations)} queries)")

        # Summary
        mitigated = sum(1 for r in results if r.get('status') == 'mitigated')
        missing = sum(1 for r in results if r.get('status') == 'missing')
        print(f"{Colors.BOLD}Summary:{Colors.RESET} {len(results)} analyzed | {Colors.GREEN}{mitigated} OK{Colors.RESET} | {Colors.RED}{missing} MISSING{Colors.RESET} | {skipped} skipped")

        return results


def main():
    parser = argparse.ArgumentParser(description='Compare x86 Spectre mitigations to RISC-V')
    parser.add_argument('kernel_path', help='Path to kernel source')
    parser.add_argument('-m', '--mitigations', default='mitigations.csv',
                       help='CSV file with mitigation sites (default: mitigations.csv)')
    parser.add_argument('-k', '--top-k', type=int, default=3, help='Number of matches to show')

    args = parser.parse_args()

    # Load mitigations from file
    mit_file = Path(args.mitigations)
    if not mit_file.exists():
        # Try relative to script location
        mit_file = Path(__file__).parent.parent / args.mitigations
    if not mit_file.exists():
        print(f"{Colors.RED}Error: {args.mitigations} not found{Colors.RESET}")
        sys.exit(1)

    mitigations = load_mitigations(mit_file)

    analyzer = MitigationAnalyzer(args.kernel_path, mitigations, top_k=args.top_k)
    analyzer.analyze()


if __name__ == '__main__':
    main()
