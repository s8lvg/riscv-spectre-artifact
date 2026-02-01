# Exploit Framework API

## Quick Start

```c
#include "../exploit_framework.h"

size_t CACHE_MISS = 0;

void my_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    secret_data[0] = encode_index;
    // trigger speculation, encode into ctx->probe_array
}

int main() {
    exploit_ctx_t ctx = {0};
    if (exploit_init(&ctx) != 0) return 1;

    exploit_hooks_t hooks = { .attack = my_attack };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("My-Variant", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
```

## Execution Modes

Mode is auto-detected based on which hooks are provided:

- **Simple**: `attack` hook (PHT/sa_*, STL, BTB/sa_*)
- **Threading**: `attacker_thread`, `victim_thread`, `pre_measure` hooks (RSB/sa_ip)
- **Forking**: `child_loop`, `parent_attack` hooks (*/ca_*)

## Hooks

```c
typedef struct {
    // Common (all modes)
    void (*setup)(exploit_ctx_t* ctx, void* data);
    void (*cleanup)(exploit_ctx_t* ctx, void* data);

    // Simple mode
    void (*attack)(exploit_ctx_t* ctx, void* data, uint8_t encode_index);

    // Threading mode
    void* (*attacker_thread)(void* data);
    void* (*victim_thread)(void* data);
    void (*pre_measure)(exploit_ctx_t* ctx, void* data);

    // Forking mode
    void (*child_loop)(exploit_ctx_t* ctx, void* data);
    void (*parent_attack)(exploit_ctx_t* ctx, void* data, uint8_t encode_index);
} exploit_hooks_t;
```

## Context Fields

Key fields in `exploit_ctx_t`:
- `probe_array[256 * PAGE_SIZE]`: Probe array for cache timing
- `cache_miss_threshold`: Auto-detected or manual via THRESHOLD env
- `iterations`: Measurement iterations (default 150)
- `secret_value`: Random value generated each iteration
- `shared_secret`: Shared memory for forking mode
- `exclude_value`: Value to exclude from stats (filters training artifacts)

## Environment Variables

```bash
THRESHOLD=157 ./variant   # Override cache threshold
CORE=3 ./variant          # Pin to CPU core
DEBUG=1 ./variant         # Show cache hits per iteration
ITERATIONS=300 ./variant  # Override iteration count
```

## Examples

See existing variants:
- Simple: `PHT/sa_ip/main.c`, `STL/main.c`
- Threading: `RSB/sa_ip/main.c`
- Forking: `PHT/ca_ip/main.c`
