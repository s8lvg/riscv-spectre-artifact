# Spectre-BTB Mitigation for uBPF Interpreter

## Overview

The computed goto dispatch (`goto *jumptable[opcode]`) compiles to an indirect
jump, which is vulnerable to Spectre-BTB attacks. This mitigation replaces the
indirect jump with a runtime-generated direct JAL instruction.

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_C_FLAGS="-DUBPF_JIT_DISPATCH" -DUBPF_ENABLE_TESTS=ON
cmake --build . --target ubpf_test --parallel
```

## How it works

1. A writable JIT slot is placed in `.text` section
2. At dispatch time, encode a JAL instruction to the handler
3. Execute `fence.i` to synchronize icache
4. Jump to the JIT slot, which executes the JAL to the handler

## Benchmark

The benchmark script requires `bc` to compute overhead ratios.

```bash
# Build baseline (no mitigation)
cmake -S . -B build_baseline -DUBPF_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build_baseline --target ubpf_test --parallel

# Build mitigated
cmake -S . -B build_mitigated -DUBPF_ENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-DUBPF_JIT_DISPATCH"
cmake --build build_mitigated --target ubpf_test --parallel

# Run benchmark against bundled BPF programs (50 iterations)
./benchmark.sh build_baseline/bin/ubpf_test build_mitigated/bin/ubpf_test \
  ../bpf-benchmark/bpf_progs 50
```

## Files

- `vm/ubpf_vm.c` - JIT dispatch implementation (search for `UBPF_JIT_DISPATCH`)
