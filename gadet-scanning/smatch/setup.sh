#!/bin/bash
# Setup Smatch for Spectre gadget detection

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMATCH_DIR="${SCRIPT_DIR}/smatch-src"

echo "=== Smatch Setup ==="

if [ ! -d "${SMATCH_DIR}" ]; then
    echo "Cloning Smatch..."
    git clone https://github.com/error27/smatch.git "${SMATCH_DIR}"
else
    echo "Updating Smatch..."
    cd "${SMATCH_DIR}"
    git pull
fi

echo "Building Smatch..."
cd "${SMATCH_DIR}"
make clean || true
NCPUS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
make -j"${NCPUS}"

if [ ! -f "${SMATCH_DIR}/smatch" ]; then
    echo "ERROR: Build failed"
    exit 1
fi

echo "Done. Run: ../run-analysis.py <kernel-version> /path/to/kernel --tool smatch"
