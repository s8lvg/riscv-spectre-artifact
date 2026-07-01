#!/usr/bin/env python3
"""
Test runner for all Spectre exploit variants.
Compiles and runs each variant, reports confusion matrix metrics.
"""
import subprocess
import re
import sys
import os
import argparse
import statistics
import math
import csv
from datetime import datetime

# ANSI color codes
class Colors:
    """ANSI color code constants."""
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RESET = "\033[0m"

def color(text, color_code):
    """Apply color to text."""
    return f"{color_code}{text}{Colors.RESET}"

def calc_ci_95(values):
    """Calculate 95% confidence interval using standard error."""
    if len(values) < 2:
        mean = values[0] if values else 0
        return mean, mean, mean
    mean = statistics.mean(values)
    stddev = statistics.stdev(values)
    se = stddev / math.sqrt(len(values))
    ci_low = mean - 1.96 * se
    ci_high = mean + 1.96 * se
    return mean, ci_low, ci_high

# Define all available exploit variants
ALL_VARIANTS = {
    # PHT variants
    "PHT/sa_ip": {
        "name": "PHT same-address in-place",
        "type": "pht",
        "makefile": "spectre/PHT/sa_ip/Makefile",
        "binary": "spectre/PHT/sa_ip/sa_ip"
    },
    "PHT/sa_oop": {
        "name": "PHT same-address out-of-place",
        "type": "pht",
        "makefile": "spectre/PHT/sa_oop/Makefile",
        "binary": "spectre/PHT/sa_oop/sa_oop"
    },
    "PHT/ca_ip": {
        "name": "PHT cross-address in-place",
        "type": "pht",
        "makefile": "spectre/PHT/ca_ip/Makefile",
        "binary": "spectre/PHT/ca_ip/ca_ip"
    },
    "PHT/ca_oop": {
        "name": "PHT cross-address out-of-place",
        "type": "pht",
        "makefile": "spectre/PHT/ca_oop/Makefile",
        "binary": "spectre/PHT/ca_oop/ca_oop"
    },
    # RSB variants
    "RSB/sa_ip": {
        "name": "RSB same-address in-place",
        "type": "rsb",
        "makefile": "spectre/RSB/sa_ip/Makefile",
        "binary": "spectre/RSB/sa_ip/rsb_sa_ip"
    },
    "RSB/sa_oop": {
        "name": "RSB same-address out-of-place",
        "type": "rsb",
        "makefile": "spectre/RSB/sa_oop/Makefile",
        "binary": "spectre/RSB/sa_oop/rsb_sa_oop"
    },
    "RSB/ca_ip": {
        "name": "RSB cross-address in-place",
        "type": "rsb",
        "makefile": "spectre/RSB/ca_ip/Makefile",
        "binary": "spectre/RSB/ca_ip/rsb_ca_ip"
    },
    "RSB/ca_oop": {
        "name": "RSB cross-address out-of-place",
        "type": "rsb",
        "makefile": "spectre/RSB/ca_oop/Makefile",
        "binary": "spectre/RSB/ca_oop/rsb_ca_oop"
    },
    # STL variant
    "STL": {
        "name": "STL store-to-load forwarding",
        "type": "stl",
        "makefile": "spectre/STL/Makefile",
        "binary": "spectre/STL/stl"
    },
    # BTB variants
    "BTB/sa_ip": {
        "name": "BTB same-address in-place",
        "type": "btb",
        "makefile": "spectre/BTB/sa_ip/Makefile",
        "binary": "spectre/BTB/sa_ip/btb_sa_ip"
    },
    "BTB/sa_oop": {
        "name": "BTB same-address out-of-place",
        "type": "btb",
        "makefile": "spectre/BTB/sa_oop/Makefile",
        "binary": "spectre/BTB/sa_oop/btb_sa_oop"
    },
    "BTB/ca_ip": {
        "name": "BTB cross-address in-place",
        "type": "btb",
        "makefile": "spectre/BTB/ca_ip/Makefile",
        "binary": "spectre/BTB/ca_ip/btb_ca_ip"
    },
    "BTB/ca_oop": {
        "name": "BTB cross-address out-of-place",
        "type": "btb",
        "makefile": "spectre/BTB/ca_oop/Makefile",
        "binary": "spectre/BTB/ca_oop/btb_ca_oop"
    },
    # Misc variants (not part of standard tf-tree)
    "PHT/sa_ip_imm": {
        "name": "PHT sa-ip (immediate bounds)",
        "type": "misc",
        "makefile": "spectre/PHT/sa_ip_imm/Makefile",
        "binary": "spectre/PHT/sa_ip_imm/sa_ip_imm"
    },
    "PHT/call_redirect": {
        "name": "PHT call redirect",
        "type": "misc",
        "makefile": "spectre/PHT/call_redirect/Makefile",
        "binary": "spectre/PHT/call_redirect/call_redirect"
    },
    "RSB/btb_fallback": {
        "name": "RSB BTB fallback",
        "type": "misc",
        "makefile": "spectre/RSB/btb_fallback/Makefile",
        "binary": "spectre/RSB/btb_fallback/rsb_btb_fallback"
    },
    "RSB/sls_fallback": {
        "name": "RSB SLS fallback",
        "type": "misc",
        "makefile": "spectre/RSB/sls_fallback/Makefile",
        "binary": "spectre/RSB/sls_fallback/rsb_sls_fallback"
    },
    # Baseline test
    "BASELINE": {
        "name": "Baseline cache encoding",
        "type": "baseline",
        "makefile": "tools/baseline/Makefile",
        "binary": "tools/baseline/baseline"
    }
}

# Test groups
PHT_VARIANTS = ["PHT/sa_ip", "PHT/sa_oop", "PHT/ca_ip", "PHT/ca_oop"]
BTB_VARIANTS = ["BTB/sa_ip", "BTB/sa_oop", "BTB/ca_ip", "BTB/ca_oop"]
RSB_VARIANTS = ["RSB/sa_ip", "RSB/sa_oop", "RSB/ca_ip", "RSB/ca_oop"]
STL_VARIANTS = ["STL"]
MISC_VARIANTS = ["PHT/sa_ip_imm", "PHT/call_redirect", "RSB/btb_fallback", "RSB/sls_fallback", "STL/diff_addr", "STL/nop_slide"]

TEST_GROUPS = {
    "all": PHT_VARIANTS + BTB_VARIANTS + RSB_VARIANTS + STL_VARIANTS,
    "pht": PHT_VARIANTS,
    "btb": BTB_VARIANTS,
    "rsb": RSB_VARIANTS,
    "stl": STL_VARIANTS,
    "misc": MISC_VARIANTS,
    "baseline": ["BASELINE"],
    "same_address": ["PHT/sa_ip", "PHT/sa_oop", "BTB/sa_ip", "BTB/sa_oop", "RSB/sa_ip", "RSB/sa_oop"],
    "cross_address": ["PHT/ca_ip", "PHT/ca_oop", "BTB/ca_ip", "BTB/ca_oop", "RSB/ca_ip", "RSB/ca_oop"],
    "in_place": ["PHT/sa_ip", "PHT/ca_ip", "BTB/sa_ip", "BTB/ca_ip", "RSB/sa_ip", "RSB/ca_ip"],
    "out_of_place": ["PHT/sa_oop", "PHT/ca_oop", "BTB/sa_oop", "BTB/ca_oop", "RSB/sa_oop", "RSB/ca_oop"]
}

DEFAULT_RUNS = 5
DEFAULT_TIMEOUT = 120

def detect_cpu():
    """Detect CPU type (C910 or P550) by checking /proc/cpuinfo."""
    try:
        with open("/proc/cpuinfo", "r", encoding="utf-8") as f:
            cpuinfo = f.read()

        # Check for T-Head C910
        if "T-Head" in cpuinfo or "c910" in cpuinfo.lower():
            return "C910"

        # Check for SiFive P550
        if ("SiFive" in cpuinfo or "p550" in cpuinfo.lower()):
            return "P550"

        return None
    except (OSError, subprocess.CalledProcessError):
        return None

def check_huge_pages():
    """Check if huge pages are available (required for P550 cache eviction)."""
    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as f:
            meminfo = f.read()

        total_match = re.search(r"HugePages_Total:\s+(\d+)", meminfo)
        free_match = re.search(r"HugePages_Free:\s+(\d+)", meminfo)

        if total_match and free_match:
            total = int(total_match.group(1))
            free = int(free_match.group(1))
            return total > 0 and free > 0

        return False
    except OSError:
        return False

def build_variant(variant_key, cpu_platform=None, opt_level=None,
                  disable_flush=False, spec_fence=None):
    """Build a specific exploit variant."""
    variant = ALL_VARIANTS[variant_key]
    makefile_dir = os.path.dirname(variant["makefile"])
    binary_name = variant["binary"]

    # Auto-detect CPU if not provided
    if cpu_platform is None:
        cpu_platform = detect_cpu()

    platform_label = f"[{cpu_platform}]" if cpu_platform else ""
    fence_label = f" [fence: {spec_fence}]" if spec_fence else ""
    print(f"  {color(variant_key, Colors.DIM)} "
          f"{color(platform_label, Colors.BLUE)}{color(fence_label, Colors.YELLOW)}", end=" ", flush=True)

    try:
        # Always clean first (force rebuild)
        subprocess.run(
            ["make", "clean"],
            cwd=makefile_dir,
            capture_output=True,
            text=True,
            timeout=10,
            check=False
        )

        # Get the output binary name (filename only)
        output_basename = os.path.basename(binary_name)

        # Build make command with optional flags
        make_cmd = ["make", output_basename]
        if opt_level:
            make_cmd.append(f"OPT_LEVEL={opt_level}")
        if disable_flush:
            make_cmd.append("USE_EVICTION=1")
        if spec_fence:
            make_cmd.append(f"SPEC_FENCE={spec_fence}")

        # Build the binary
        result = subprocess.run(
            make_cmd,
            cwd=makefile_dir,
            capture_output=True,
            text=True,
            timeout=30,
            check=False
        )

        if result.returncode == 0 and os.path.exists(binary_name):
            print(color("✓", Colors.GREEN))
            return binary_name

        print(color("✗", Colors.RED))
        print(f"    Error: {result.stderr[:200]}")
        return None
    except (OSError, subprocess.TimeoutExpired) as e:
        print(color(f"✗ ({e})", Colors.RED))
        return None

def parse_exploit_output(output):
    """Parse exploit output using machine-readable KEY=VALUE section.

    Expects --- RESULTS --- markers with KEY=VALUE pairs.
    """
    result = {}

    # Parse machine-readable format
    results_match = re.search(r'--- RESULTS ---\n(.*?)\n--- END RESULTS ---',
                              output, re.DOTALL)
    if not results_match:
        return result  # Return empty dict if no results section found

    # Parse KEY=VALUE pairs
    results_section = results_match.group(1)
    for line in results_section.split('\n'):
        line = line.strip()
        if '=' in line:
            key, value = line.split('=', 1)
            key = key.strip()
            value = value.strip()

            # Parse values based on expected type
            if key in ['iterations', 'tp', 'fn', 'fp', 'tn']:
                result[key] = int(value)
            elif key in ['threshold', 'timing_avg', 'timing_min']:
                result[key] = int(value)
            elif key in ['precision', 'recall', 'accuracy', 'f1']:
                result[key] = float(value)
            elif key in ['variant', 'compiler', 'opt_level']:
                result[key] = value

    # Map to legacy field names for compatibility with existing code
    if 'tp' in result:
        result['true_positives'] = result['tp']
    if 'fn' in result:
        result['false_negatives'] = result['fn']
    if 'fp' in result:
        result['false_positives'] = result['fp']
    if 'tn' in result:
        result['true_negatives'] = result['tn']
    if 'timing_avg' in result:
        result['avg_timing'] = result['timing_avg']
    if 'timing_min' in result:
        result['min_timing'] = result['timing_min']

    return result

def run_variant(variant_key, threshold=None, core=None,
                timeout=DEFAULT_TIMEOUT, binary_path=None, debug=False,
                iterations=None):
    """Run a single exploit variant and extract results."""
    variant = ALL_VARIANTS[variant_key]

    # Use custom binary path if provided, otherwise use default
    if binary_path is None:
        binary_path = variant["binary"]

    # Set up environment
    env = os.environ.copy()
    if threshold is not None:
        env["THRESHOLD"] = str(threshold)
    if core is not None:
        env["CORE"] = str(core)
    if debug:
        env["DEBUG"] = "1"
    if iterations is not None:
        env["ITERATIONS"] = str(iterations)

    try:
        result = subprocess.run(
            ["sudo", "-E", f"./{binary_path}"],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
            check=False
        )
        output = result.stdout + result.stderr

        # Extract threshold and core
        threshold_match = re.search(r"Threshold:\s+(\d+)", output)
        core_match = re.search(r"Core:\s+(\d+)", output)
        detected_threshold = (int(threshold_match.group(1))
                              if threshold_match else None)
        detected_core = (int(core_match.group(1))
                         if core_match else None)

        # Parse output
        result_data = parse_exploit_output(output)
        result_data.update({
            "threshold": detected_threshold,
            "core": detected_core,
            "output": output
        })

        return result_data

    except subprocess.TimeoutExpired:
        return {
            "success": False,
            "error": "TIMEOUT",
            "threshold": threshold,
            "core": core
        }
    except OSError as e:
        return {
            "success": False,
            "error": str(e),
            "threshold": threshold,
            "core": core
        }

def print_result(result, run_num, total_runs, debug=False):
    """Print result for a single run (concise, colored)."""
    if "error" in result:
        print(f"  {color('✗', Colors.RED)} Run {run_num}/"
              f"{total_runs}: {result['error']}")
        return

    # Status based on whether we got valid results
    status = color("·", Colors.CYAN)

    # Metrics with color coding
    if "recall" in result:
        recall = result["recall"]
        precision = result.get("precision", 0)
        f1 = result.get("f1", 0)

        # F1 with color coding
        if f1 >= 90:
            f1_str = color(f"{f1:5.1f}%", Colors.GREEN)
        elif f1 >= 50:
            f1_str = color(f"{f1:5.1f}%", Colors.YELLOW)
        else:
            f1_str = color(f"{f1:5.1f}%", Colors.RED)

        # Concise format: F1, precision, recall
        print(f"  {status} Run {run_num}/{total_runs}: "
              f"F1={f1_str}  P={precision:5.1f}%  R={recall:5.1f}%")
    else:
        print(f"  {status} Run {run_num}/{total_runs}")

    # In debug mode, also print the full output (including DEBUG lines)
    if debug and "output" in result:
        lines = [line for line in result["output"].split('\n') if line.strip()]
        if lines:
            print(f"{color('    [Full Output]:', Colors.DIM)}")
            for line in lines:
                print(f"    {line}")

def print_summary(results):
    """Print summary statistics for all runs of a variant."""
    runs = len(results)
    valid_runs = sum(1 for r in results if "recall" in r)

    # Collect all metrics
    recalls = [r["recall"] for r in results if "recall" in r]
    precisions = [r["precision"] for r in results if "precision" in r]

    if recalls:
        avg_recall, recall_ci_low, recall_ci_high = calc_ci_95(recalls)
        avg_precision = statistics.mean(precisions) if precisions else 0

        # Clamp CI to valid range
        recall_ci_low = max(0, recall_ci_low)
        recall_ci_high = min(100, recall_ci_high)

        # Color code based on recall
        if avg_recall >= 80:
            recall_color = Colors.GREEN
        elif avg_recall >= 50:
            recall_color = Colors.YELLOW
        else:
            recall_color = Colors.RED

        print(f"  {color('→', Colors.CYAN)} "
              f"R: {color(f'{avg_recall:.1f}%', recall_color)} [{recall_ci_low:.1f},{recall_ci_high:.1f}]  "
              f"P: {avg_precision:.1f}%  "
              f"(n={valid_runs})")
    else:
        print(f"  {color('→', Colors.CYAN)} "
              f"No valid results (n={valid_runs}/{runs})")

def setup_test_environment():
    """Detect platform and validate prerequisites."""
    cpu_platform = detect_cpu()

    # P550-specific checks
    if cpu_platform == "P550":
        if not check_huge_pages():
            err_msg = color("ERROR: Huge pages not configured",
                            Colors.RED + Colors.BOLD)
            print(f"\n{err_msg}")
            print("P550 requires huge pages for cache eviction.")
            print("\nTo allocate hugepages (as root):")
            print("  sudo sysctl -w vm.nr_hugepages=64")
            sys.exit(1)

    return cpu_platform

def select_variants(args, parser):
    """Determine which variants to test based on arguments."""
    if args.variants:
        return args.variants
    if args.group:
        return TEST_GROUPS[args.group]

    print("ERROR: Must specify either --group or --variants")
    parser.print_help()
    sys.exit(1)

def print_test_header(cpu_platform, variants_to_test, args):
    """Print test configuration header."""
    header = color("Spectre Exploit Test Runner",
                   Colors.BOLD + Colors.CYAN)
    print(f"\n{header}")
    print(color("─" * 50, Colors.DIM))

    # Display detected platform
    if cpu_platform:
        platform_color = (Colors.GREEN if cpu_platform in ["C910", "P550"]
                          else Colors.YELLOW)
        print(f"Platform: {color(cpu_platform, platform_color)}")
    else:
        unknown = color("Unknown (generic build)", Colors.YELLOW)
        print(f"Platform: {unknown}")

    print(f"Testing {color(str(len(variants_to_test)), Colors.BOLD)} "
          f"variants × {color(str(args.runs), Colors.BOLD)} runs", end="")
    if args.threshold:
        threshold_str = color(str(args.threshold), Colors.YELLOW)
        print(f" | Threshold: {threshold_str}", end="")
    if args.core is not None:
        core_str = color(str(args.core), Colors.YELLOW)
        print(f" | Core: {core_str}", end="")
    separator = color("─" * 50, Colors.DIM)
    print(f"\n{separator}\n")

def test_all_variants(variants_to_test, args, cpu_platform):
    """Run tests for all selected variants."""
    print("Testing variants...\n")
    all_results = {}

    for variant_key in variants_to_test:
        variant = ALL_VARIANTS[variant_key]
        print(f"{color(variant['name'], Colors.BOLD)} "
              f"{color(f'({variant_key})', Colors.DIM)}")

        # Build binary
        binary = build_variant(variant_key,
                              cpu_platform=cpu_platform,
                              opt_level=getattr(args, 'opt_level', None),
                              disable_flush=getattr(args, 'disable_flush', False),
                              spec_fence=getattr(args, 'fence', None))
        if binary is None:
            print(f"  {color('✗ Build failed', Colors.RED)}\n")
            continue

        # Run tests
        results = []
        debug_mode = getattr(args, 'debug', False)
        iterations = getattr(args, 'iterations', None)
        for run_num in range(1, args.runs + 1):
            result = run_variant(variant_key, args.threshold, args.core,
                                 args.timeout, binary_path=binary,
                                 debug=debug_mode, iterations=iterations)
            results.append(result)
            print_result(result, run_num, args.runs, debug=debug_mode)

        all_results[variant_key] = results
        print_summary(results)
        print()

    return all_results

def print_overall_summary(all_results):
    """Print overall summary of all test results."""
    summary_header = color("Overall Summary", Colors.BOLD + Colors.CYAN)
    print(f"\n{summary_header}")
    print(color("─" * 70, Colors.DIM))

    for variant_key, results in all_results.items():
        variant = ALL_VARIANTS[variant_key]
        valid_runs = sum(1 for r in results if "recall" in r)

        # Calculate metrics with 95% CI
        recalls = [r["recall"] for r in results if "recall" in r]
        precisions = [r["precision"] for r in results if "precision" in r]
        f1s = [r["f1"] for r in results if "f1" in r]

        avg_recall, recall_ci_low, recall_ci_high = calc_ci_95(recalls) if recalls else (0, 0, 0)
        avg_precision = statistics.mean(precisions) if precisions else 0
        avg_f1 = statistics.mean(f1s) if f1s else 0

        # Clamp CI to valid range
        recall_ci_low = max(0, recall_ci_low)
        recall_ci_high = min(100, recall_ci_high)

        # Color code based on recall
        if avg_recall >= 80:
            recall_str = color(f"{avg_recall:5.1f}%", Colors.GREEN)
        elif avg_recall >= 50:
            recall_str = color(f"{avg_recall:5.1f}%", Colors.YELLOW)
        else:
            recall_str = color(f"{avg_recall:5.1f}%", Colors.RED)

        print(f"  {variant['name']:35} "
              f"R={recall_str} [{recall_ci_low:4.1f},{recall_ci_high:5.1f}]  "
              f"P={avg_precision:5.1f}%  (n={valid_runs})")

    final_sep = color("─" * 70, Colors.DIM)
    print(f"{final_sep}\n")

def export_csv(all_results, csv_path, cpu_platform):
    """Export results to CSV file."""
    with open(csv_path, 'w', newline='') as f:
        writer = csv.writer(f)
        # Header
        writer.writerow([
            'timestamp', 'platform', 'variant', 'variant_name', 'run',
            'recall', 'precision', 'f1', 'tp', 'fn', 'fp', 'tn',
            'threshold', 'timing_avg', 'timing_min'
        ])

        timestamp = datetime.now().isoformat()
        for variant_key, results in all_results.items():
            variant = ALL_VARIANTS[variant_key]
            for i, r in enumerate(results, 1):
                writer.writerow([
                    timestamp,
                    cpu_platform,
                    variant_key,
                    variant['name'],
                    i,
                    r.get('recall', ''),
                    r.get('precision', ''),
                    r.get('f1', ''),
                    r.get('tp', ''),
                    r.get('fn', ''),
                    r.get('fp', ''),
                    r.get('tn', ''),
                    r.get('threshold', ''),
                    r.get('timing_avg', ''),
                    r.get('timing_min', '')
                ])
    print(f"Results exported to: {csv_path}")

def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Test runner for all Spectre exploit variants",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Available test groups:
  all              - All standard variants (13: PHT + BTB + RSB + STL)
  pht              - PHT variants (4)
  btb              - BTB variants (4)
  rsb              - RSB variants (4)
  stl              - STL variant (1)
  misc             - Misc experiments (6: sa_ip_imm, call_redirect, fallbacks, STL variants)
  baseline         - Baseline cache encoding test (1)
  same_address     - Same-address variants (6)
  cross_address    - Cross-address variants (6)
  in_place         - In-place variants (6)
  out_of_place     - Out-of-place variants (6)

Examples:
  # Run all tests with auto-detected threshold
  %(prog)s --group all --runs 5

  # Run PHT variants with manual threshold on core 1
  %(prog)s --group pht --threshold 150 --core 1 --runs 3

  # Run specific variants with optimization level 0 (debugging)
  %(prog)s --variants PHT/sa_ip RSB/ca_ip --runs 10 --opt-level 0
"""
    )

    parser.add_argument("--group", choices=TEST_GROUPS.keys(),
                        help="Run predefined test group")
    parser.add_argument("--variants", nargs="+",
                        choices=ALL_VARIANTS.keys(),
                        help="Run specific variants")
    parser.add_argument("--runs", type=int, default=DEFAULT_RUNS,
                        help=f"Number of runs per variant "
                             f"(default: {DEFAULT_RUNS})")
    parser.add_argument("--threshold", type=int,
                        help="Manual cache threshold (default: auto-detect)")
    parser.add_argument("--core", type=int,
                        help="CPU core to pin to (default: 0)")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT,
                        help=f"Timeout per run in seconds "
                             f"(default: {DEFAULT_TIMEOUT})")
    parser.add_argument("--opt-level",
                        choices=["0", "1", "2", "3", "s"],
                        help="Override compiler optimization level (default: s)")
    parser.add_argument("--disable-flush", action="store_true",
                        help="Disable flush instruction (forces cache eviction)")
    parser.add_argument("--debug", action="store_true",
                        help="Enable debug mode (shows cache hits)")
    parser.add_argument("--iterations", type=int,
                        help="Number of measurement iterations (default: 1000 for most variants)")
    parser.add_argument("--csv", type=str, metavar="PATH",
                        help="Export results to CSV file")
    parser.add_argument("--fence", type=str, metavar="INSN",
                        help="Insert speculation fence before encoding (e.g., 'rdtime x0'). "
                             "Verifies attack is speculative and fence effectiveness.")

    args = parser.parse_args()

    # Setup environment and validate prerequisites
    cpu_platform = setup_test_environment()

    # Determine which variants to test
    variants_to_test = select_variants(args, parser)

    # Print configuration header
    print_test_header(cpu_platform, variants_to_test, args)

    # Run all tests
    all_results = test_all_variants(variants_to_test, args, cpu_platform)

    # Print overall summary
    print_overall_summary(all_results)

    # Export CSV if requested
    if args.csv:
        export_csv(all_results, args.csv, cpu_platform)

if __name__ == "__main__":
    main()
