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
./run-analysis.py linux-6.6 ~/linux-riscv

# Run single tool
./run-analysis.py linux-6.6 ~/linux-riscv --tool smatch
./run-analysis.py linux-6.6 ~/linux-riscv --tool codeql

# Compare tools (Smatch vs CodeQL)
./compare-tools.py linux-6.6
```

## Kernel Sources for lab64

The lab64 machine runs **T-Head TH1520** (MilkV Meles) with kernel **5.10.113-th1520** built by RevyOS.

### Getting the Kernel Source

**Primary source repository:**
```bash
git clone https://github.com/revyos/thead-kernel.git
cd thead-kernel
git checkout lpi4a
```

**Alternative (MilkV Meles-specific):**
```bash
git clone https://github.com/milkv-meles/thead-kernel.git
cd thead-kernel
git checkout meles
```

Both repositories contain **Linux 5.10.113** with T-Head TH1520-specific patches.

### Kernel Information

**On lab64:**
- **Kernel**: 5.10.113-th1520
- **Build date**: May 31, 2024
- **Builder**: RevyOS (builder@revyos-riscv-builder)
- **Chip**: T-Head TH1520 (quad-core C910 + C906 DSP)

### Configuring the Kernel

```bash
cd thead-kernel

# Get the running kernel config from lab64
scp lab64:/boot/config-5.10.113-th1520 .config

# Or use the default TH1520 config
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- th1520_defconfig

# Prepare for analysis
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- prepare
```

### Running Analysis on lab64 Kernel

```bash
git clone https://github.com/revyos/thead-kernel.git ~/thead-kernel
cd ~/thead-kernel && git checkout lpi4a

scp lab64:/boot/config-5.10.113-th1520 .config

cd ~/experiments/spectre-gadget-scan
./run-analysis.py th1520-5.10 ~/thead-kernel --use-existing-config
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
   ./run-analysis.py linux-6.6 ~/linux-riscv
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
