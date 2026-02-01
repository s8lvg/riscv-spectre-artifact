# Branchless JIT Dispatch for RISC-V

Spectre-BTB mitigation that JIT-compiles direct branches instead of using retpolines. Inspired by Switchpoline.

## How it works

1. Encode target address into `jal` instruction using branchless arithmetic
2. Write instruction to executable JIT slot
3. `fence.i` + direct jump executes JIT'd code
4. No indirect branches, no RSB dependency, no speculation fence needed

## Build & Run

```bash
ssh lab46
cd experiments/branchless-jit-dispatch
make
./dispatch
```

Benchmarks normal indirect calls vs JIT dispatch (10,000 trials each).

## Limitations

- JAL range: ±1MB from JIT slot
- Single JIT slot (not thread-safe)
- `fence.i` overhead per call

## uBPF Benchmark

Real-world implementation in the uBPF interpreter. See `ubpf/SPECTRE_MITIGATION.md` for build instructions and benchmark results.
