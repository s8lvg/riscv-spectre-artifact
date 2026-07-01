# RSB Size Test

Measures Return Stack Buffer (RSB) depth on RISC-V processors.

## Method

Uses JIT-generated nested function calls of increasing depth:
- `fun[0]`: records the start timestamp, then returns
- `fun[N]`: calls `fun[N-1]`, then returns

We time after all calls have been executed until the end of the return.
This prevents measuring the overhead the calls have. 

## Usage

```bash
make
./test
```

Results printed to stdout and saved to `log.txt`. Deltas are saved to
`log_delta.txt`.

## Implementation

- Uses `libjit.h` to generate compact PC-relative call chains
- Auto-detects C910/P550 for timing infrastructure
