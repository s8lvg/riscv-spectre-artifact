# Speculation Window Measurement

Measures speculation window length with different branch dependencies using `bltu` instruction.

## Dependency Types
1. **mem_cached** - bltu with cached memory load
2. **mem_uncached** - bltu with uncached memory load
3. **immediate** - bltu with immediate value
4. **stalled_imm** - bltu with stalled immediate (100-iteration dependency chain)

## Test Pattern
All tests use: `if (value >= bound) skip_probe` where value=100 and bound=10

## Building
```bash
make
./main
```

## Output
CSV format: NOPs, probe_time, speculated (0/1)
- If speculation successful: probe cached (fast access time)
- If speculation window too short: probe not cached (slow access time)
