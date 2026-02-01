# LLVM RISC-V Compilers with Spectre Mitigations

Docker builds for various LLVM forks with RISC-V Spectre mitigations.

## Available Compilers

| Dockerfile | Source | Mitigation |
|------------|--------|------------|
| `Dockerfile.llvm-slh` | [LLVM-SLH-RISCV](https://github.com/MoeinGhaniyoun/LLVM-SLH-RISCV) | SLH + fence.i |
| `Dockerfile.llvm-retpoline` | [llvm_retpoline](https://github.com/riscv-spectre-mitigations/llvm_retpoline) | Retpoline |
| `Dockerfile.llvm-riscv` | [llvm-fence-spec](https://gitlab.inria.fr/arsene-pepr/llvm-fence-spec) | INRIA (buggy) |

## Build

```bash
# SLH compiler (recommended for Spectre-PHT)
DOCKER_BUILDKIT=1 docker build -f Dockerfile.llvm-slh -t llvm-slh .

# Retpoline compiler (for Spectre-BTB)
DOCKER_BUILDKIT=1 docker build -f Dockerfile.llvm-retpoline -t llvm-retpoline .
```

## Export

```bash
# Extract compiler for use with specbuild
docker run --rm llvm-slh tar -C /opt -cf - llvm-riscv | gzip > llvm-slh.tar.gz
docker run --rm llvm-retpoline tar -C /opt -cf - llvm-riscv | gzip > llvm-retpoline.tar.gz
```

## Compiler Flags

### SLH (llvm-slh)
```bash
clang --target=riscv64-linux-gnu \
    -mllvm -riscv-speculative-load-hardening \
    -mllvm -riscv-slh-fence \
    -O2 -c test.c
```

### Retpoline (llvm-retpoline)
```bash
clang --target=riscv64-linux-gnu \
    -mretpoline \
    -O2 -c test.c
```

## Notes

- INRIA compiler (`Dockerfile.llvm-riscv`) produces buggy code, do not use
- SLH compiler inserts fence.i after loads to prevent speculative execution
- Retpoline compiler protects indirect branches (Spectre v2/BTB)
