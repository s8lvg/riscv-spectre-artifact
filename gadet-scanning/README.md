# Spectre Gadget Detection for Linux Kernel (RISC-V)

Spectre v1 gadget detection for RISC-V using Smatch, CodeQL, and mitigation diffing.

## Prerequisites

Recommended setup:

1. **Docker** on an x86_64 Linux host
2. **Disk space**: ~50GB for kernel builds and CodeQL databases
3. **Kernel source**: Linux v6.6 source tree

The Docker image contains the RISC-V cross compiler, Smatch, CodeQL, and the
Python dependencies for mitigation diffing.

## Docker Quick Start

```bash
# Build the analysis image.
cd /path/to/riscv-spectre-artifact/gadet-scanning
docker build -t riscv-gadget-scan .

# Get the kernel source.
git clone --depth 1 --branch v6.6 https://github.com/torvalds/linux.git ~/linux-6.6
mkdir -p results .hf-cache mitigation_diffing/.embedding_cache

# Run Smatch.
docker run --rm \
  -v ~/linux-6.6:/kernel \
  -v "$PWD/results:/opt/gadet-scanning/results" \
  riscv-gadget-scan \
  ./run-analysis.py linux-6.6 /kernel --tool smatch

# Run CodeQL. This creates a reusable database in results/linux-6.6/codeql-db/.
docker run --rm \
  -v ~/linux-6.6:/kernel \
  -v "$PWD/results:/opt/gadet-scanning/results" \
  riscv-gadget-scan \
  ./run-analysis.py linux-6.6 /kernel --tool codeql

# Run mitigation diffing. The cache mounts avoid repeated model downloads and
# repeated RISC-V embedding generation.
docker run --rm \
  -v ~/linux-6.6:/kernel:ro \
  -v "$PWD/.hf-cache:/root/.cache" \
  -v "$PWD/mitigation_diffing/.embedding_cache:/opt/gadet-scanning/mitigation_diffing/.embedding_cache" \
  riscv-gadget-scan \
  bash -lc 'cd mitigation_diffing && mitigation-diffing /kernel -m mitigations.csv'

# Compare tools (Smatch vs CodeQL)
docker run --rm \
  -v "$PWD/results:/opt/gadet-scanning/results" \
  riscv-gadget-scan \
  ./compare-tools.py linux-6.6
```

`./run-analysis.py linux-6.6 /kernel` runs both Smatch and CodeQL. Running the
tools separately is often more convenient because the full CodeQL database build
is substantially heavier than the Smatch pass.

## Kernel Source

The results in the paper are produced against **mainline Linux v6.6** (the kernel version shipping with the C910).

### Getting the Kernel Source

```bash
git clone --depth 1 --branch v6.6 https://github.com/torvalds/linux.git ~/linux-6.6
```

### Kernel Configuration

The Docker workflow uses the shipped RISC-V config
`configs/linux-6.6-riscv-smatch.config`. The analysis script copies it into the
kernel tree and runs `make olddefconfig` before Smatch or CodeQL. The config
disables Nouveau because Smatch reports non-Spectre checker errors in that
driver on Linux v6.6.

Mitigation diffing needs only the source tree, no kernel configuration or build.

### Manual Setup

If Docker is not available, install the dependencies manually:

```bash
sudo apt-get install build-essential gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
  bc bison flex git libelf-dev libncurses-dev libsqlite3-dev libssl-dev make \
  pkg-config python3 python3-pip python3-venv sqlite3 universal-ctags unzip zstd

cd /path/to/riscv-spectre-artifact/gadet-scanning
cd smatch && ./setup.sh
cd ../codeql && ./setup.sh
cd ../mitigation_diffing && python3 -m pip install -e .
```

### Running Analysis

```bash
cd /path/to/gadet-scanning
./run-analysis.py linux-6.6 ~/linux-6.6
cd mitigation_diffing
mitigation-diffing ~/linux-6.6 -m mitigations.csv
```

## Directory Structure

```
spectre-gadget-scan/
  smatch/                           # Smatch setup and source
  codeql/                           # CodeQL setup, queries
  configs/                          # Shipped kernel configs for reproducible scans
  mitigation_diffing/               # Cross-arch mitigation comparison (pip package)
  results/
    <kernel-version>/               # per-kernel results
      kernel/                       # symlink to kernel source
      codeql-db/db/                 # CodeQL database (reusable)
      smatch/
        spectre_warnings.txt
        smatch_full.log
      codeql/
        spectre-v1-gadgets.csv
        spectre-v1-gadgets.sarif
      comparison/                   # cross-tool comparisons
```

## Workflow

1. **Run Smatch and CodeQL**:
   ```bash
   ./run-analysis.py linux-6.6 ~/linux-6.6
   ```

2. **Run mitigation diffing**:
   ```bash
   cd mitigation_diffing
   mitigation-diffing ~/linux-6.6 -m mitigations.csv
   ```

3. **Compare Smatch vs CodeQL**:
   ```bash
   ./compare-tools.py linux-6.6
   ```

4. **Sync results to local machine**:
   ```bash
   ./sync-from-remote.py linux-6.6
   ```

5. **Test exploitability** on hardware:
   - C910 (lab64)
   - P550 (lab77)

## Scripts

- `run-analysis.py`: Run Smatch and/or CodeQL analysis on a kernel
- `compare-tools.py`: Compare Smatch vs CodeQL results
- `sync-from-remote.py`: Sync results from remote machines

## Mitigation Diffing

Cross-architecture comparison of Spectre mitigations using semantic code embeddings. Finds RISC-V equivalents of known x86 mitigation sites.

```bash
cd mitigation_diffing
pip install -e .
mitigation-diffing /path/to/kernel
```

See [`mitigation_diffing/README.md`](mitigation_diffing/README.md) for details.

## Tool Details

- **Smatch**: Pattern-based static analyzer (`smatch/smatch-src/check_spectre.c`)
- **CodeQL**: Dataflow-based analysis (`codeql/spectre-v1.ql`, IR-based)
- **Mitigation Diffing**: Semantic embedding-based cross-arch comparison (`mitigation_diffing/`)
