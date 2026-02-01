#!/bin/bash
# SPEC CPU 2017 Minimal Runner
# Usage: ./run.sh [benchmark] [--validate]
#
# Examples:
#   ./run.sh 505.mcf_r              # Run single benchmark
#   ./run.sh 505.mcf_r --validate   # Run with validation
#   ./run.sh intrate                # Run all intrate benchmarks
#   ./run.sh all                    # Run all benchmarks

set -e
SPEC_DIR="$(cd "$(dirname "$0")" && pwd)"
VALIDATE=0

# Parse args
TARGETS=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --validate) VALIDATE=1; shift ;;
        *) TARGETS+=("$1"); shift ;;
    esac
done

[[ ${#TARGETS[@]} -eq 0 ]] && TARGETS=("all")

# Expand suite names
expand_suite() {
    case $1 in
        intrate) ls -d "$SPEC_DIR/benchspec/CPU"/5[0-5]*.* 2>/dev/null | xargs -n1 basename ;;
        fprate)  ls -d "$SPEC_DIR/benchspec/CPU"/5[0-9][3-9].* "$SPEC_DIR/benchspec/CPU"/5[1-9][0-9].* 2>/dev/null | grep -v -E '50[0-2]\.|505\.|520\.|523\.|525\.|531\.|541\.|548\.|557\.' | xargs -n1 basename ;;
        all)     ls -d "$SPEC_DIR/benchspec/CPU"/5*.* 2>/dev/null | xargs -n1 basename ;;
        *)       echo "$1" ;;
    esac
}

run_benchmark() {
    local bench="$1"
    local benchdir="$SPEC_DIR/benchspec/CPU/$bench"

    if [[ ! -d "$benchdir" ]]; then
        echo "[SKIP] $bench - not found"
        return 1
    fi

    local rundir=$(ls -td "$benchdir/run/run_"* 2>/dev/null | head -1)
    if [[ -z "$rundir" || ! -f "$rundir/speccmds.cmd" ]]; then
        echo "[SKIP] $bench - no run directory"
        return 1
    fi

    echo "[RUN] $bench"
    cd "$rundir"

    local start=$(date +%s.%N)
    if "$SPEC_DIR/bin/specinvoke" -d . -f speccmds.cmd > specinvoke.stdout 2> specinvoke.stderr; then
        local status="OK"
    else
        local status="FAIL"
    fi
    local elapsed=$(echo "$(date +%s.%N) - $start" | bc)

    # Validation
    local validation=""
    if [[ $VALIDATE -eq 1 && -f compare.cmd ]]; then
        if "$SPEC_DIR/bin/specinvoke" -d . -f compare.cmd > compare.stdout 2> compare.stderr; then
            if grep -qE "specdiff.*miscompare|DIFFERENT" compare.stdout compare.stderr 2>/dev/null; then
                validation=" [MISMATCH]"
            else
                validation=" [VALID]"
            fi
        else
            validation=" [VALIDATE-ERR]"
        fi
    fi

    printf "  -> %s %.2fs%s\n" "$status" "$elapsed" "$validation"
    cd "$SPEC_DIR"
}

# Main
echo "SPEC CPU 2017 Minimal Runner"
echo "============================"

BENCHMARKS=()
for target in "${TARGETS[@]}"; do
    while IFS= read -r bench; do
        BENCHMARKS+=("$bench")
    done < <(expand_suite "$target")
done

for bench in "${BENCHMARKS[@]}"; do
    run_benchmark "$bench"
done
