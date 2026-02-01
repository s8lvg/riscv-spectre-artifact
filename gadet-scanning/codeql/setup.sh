#!/bin/bash
# Setup CodeQL for Spectre gadget detection
# Host: x86_64 Linux (lab25)
# Target: RISC-V kernel source code

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CODEQL_DIR="${SCRIPT_DIR}/codeql-home"
CODEQL_BIN="${CODEQL_DIR}/codeql/codeql"

echo "=== CodeQL Setup ==="

if [ ! -f "${CODEQL_BIN}" ]; then
    echo "Downloading CodeQL..."
    mkdir -p "${CODEQL_DIR}"
    cd "${CODEQL_DIR}"

    CODEQL_URL="https://github.com/github/codeql-cli-binaries/releases/latest/download/codeql-linux64.zip"
    curl -L -o codeql.zip "${CODEQL_URL}"
    unzip -q codeql.zip
    rm codeql.zip
else
    echo "CodeQL already installed"
fi

if [ ! -d "${CODEQL_DIR}/codeql-repo" ]; then
    echo "Downloading CodeQL queries..."
    cd "${CODEQL_DIR}"
    git clone --depth 1 https://github.com/github/codeql codeql-repo
else
    echo "CodeQL queries already installed"
fi

"${CODEQL_BIN}" version
echo ""
echo "Done. Run: ../run-analysis.py <kernel-version> /path/to/kernel --tool codeql"
