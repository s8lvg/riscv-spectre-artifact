# RSB Size Test

Measures Return Stack Buffer (RSB) depth on RISC-V processors.

## Method

Uses JIT-generated nested function calls of increasing depth:
- `fun[0]`: returns immediately
- `fun[N]`: calls `fun[N-1]`, then returns

Timing `fun[N]` reveals RSB overflow depth via performance cliff.

## Usage

```bash
make
./test
```

Results printed to stdout and saved to `log.txt`.

## Implementation

- Uses `libjit.h` to generate compact PC-relative call chains
- Filters interrupt outliers (>1000 cycles)
- Auto-detects C910/P550 for timing infrastructure
