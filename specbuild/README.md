# SPEC CPU 2017 Cross-Compilation

Cross-compile SPEC CPU 2017 for RISC-V, ARM64, and LoongArch64 targets.
Produces minimal packages that run **without runcpu** on the target.

## Prerequisites

- Docker with BuildKit support
- SPEC CPU 2017 source (not included, requires license)
- Python 3 with PyYAML (`pip install pyyaml`)
- x86_64 build host (for cross-compilation)

## Build Environment Setup

### 1. Build the specbuild Docker Image

```bash
docker build -t specbuild docker/
```

### 2. Obtain Target Sysroot

The sysroot contains headers and libraries from the target. Fetches compressed from target and fixes absolute symlinks:

```bash
./fetch-sysroot.py <host> ./sysroots/<host>-sysroot
```

### 3. Set up SPEC CPU 2017

Place your SPEC CPU 2017 installation at `cpu2017/spec_cpu/` or update the path in `spec-build.yaml`.

### 4. (Optional) Build LLVM with Spectre Mitigations

Only needed for LLVM mitigation configs (`riscv64-slh.cfg`, `riscv64-fence.cfg`, `riscv64-retpoline.cfg`):

```bash
cd compilers
DOCKER_BUILDKIT=1 docker build -f Dockerfile.llvm-riscv -t llvm-riscv .
```

See [compilers/README.md](compilers/README.md) for details on mitigations and required flags.

## Quick Start

```bash
python3 build.py -l                              # List targets
python3 build.py -t target --benchmarks=505.mcf_r # Single benchmark
python3 build.py -t target --benchmarks=intrate   # Integer suite
```

## Running on Target

```bash
scp -r packages/target target:~/cpu2017
ssh target "cd ~/cpu2017 && ./run.sh 505.mcf_r"
```
