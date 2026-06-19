# Spectre Gadget Detection for Linux Kernel (RISC-V)

Spectre v1 gadget detection for RISC-V using Smatch and CodeQL.

## Prerequisites

1. **RISC-V cross-compiler**: `riscv64-linux-gnu-gcc`
2. **Python 3.6+**
3. **Disk space**: ~50GB
4. **Kernel source**: Configured kernel tree

## Quick Start

```bash
# Setup (one time)
cd smatch && ./setup.sh
cd ../codeql && ./setup.sh
cd ..

# Run analysis (all tools: Smatch, CodeQL)
./run-analysis.py linux-6.6 ~/linux-6.6

# Run single tool
./run-analysis.py linux-6.6 ~/linux-6.6 --tool smatch
./run-analysis.py linux-6.6 ~/linux-6.6 --tool codeql

# Compare tools (Smatch vs CodeQL)
./compare-tools.py linux-6.6
```

## Kernel Source

The results in the paper are produced against **mainline Linux v6.6** (the kernel version shipping with the C910).

### Getting the Kernel Source

```bash
git clone --depth 1 --branch v6.6 https://github.com/torvalds/linux.git ~/linux-6.6
```

### Configuring the Kernel

Smatch and CodeQL build the kernel, so it must be configured for RISC-V first
(mitigation diffing needs only the source tree, no configuration or build):

```bash
cd ~/linux-6.6
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- prepare
```

### Running Analysis

```bash
cd /path/to/gadet-scanning
./run-analysis.py linux-6.6 ~/linux-6.6
```

## Directory Structure

```
spectre-gadget-scan/
  smatch/                           # Smatch setup and source
  codeql/                           # CodeQL setup, queries
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

1. **Run analysis**:
   ```bash
   ./run-analysis.py linux-6.6 ~/linux-6.6
   ```

2. **Compare Smatch vs CodeQL**:
   ```bash
   ./compare-tools.py linux-6.6
   ```

3. **Sync results to local machine**:
   ```bash
   ./sync-from-remote.py linux-6.6
   ```

4. **Test exploitability** on hardware:
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
