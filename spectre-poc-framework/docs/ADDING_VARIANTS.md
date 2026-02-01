# Adding New Variants

## 1. Create Directory

```bash
mkdir -p spectre/CATEGORY/variant_name
cd spectre/CATEGORY/variant_name
```

Naming: `sa_ip` (same-address in-place), `sa_oop`, `ca_ip`, `ca_oop`

## 2. Create Makefile

```makefile
BINARY = variant_name
include ../../../common.mk
```

## 3. Implement main.c

**Simple mode** (most common):

```c
#include "../../../exploit_framework.h"

size_t CACHE_MISS = 0;
uint8_t* probe_array;
static uint8_t secret_data[256];

void variant_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    secret_data[0] = encode_index;
    // Your attack: flush, mistrain, trigger speculation
}

int main() {
    exploit_ctx_t ctx = {0};
    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    exploit_hooks_t hooks = { .attack = variant_attack };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("CATEGORY/variant_name", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
```

**Forking mode** (cross-address):

```c
void variant_child_loop(exploit_ctx_t* ctx, void* data) {
    // Training loop (runs in child)
    for (int i = 0; i < 1000; i++) train_predictor();
}

void variant_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    // Attack (runs in parent), secret available via *ctx->shared_secret
    flush(&bounds); victim_function(malicious_idx);
}

exploit_hooks_t hooks = {
    .child_loop = variant_child_loop,
    .parent_attack = variant_parent_attack
};
```

## 4. Add to Test Runner

Edit `run_all_tests.py`:

```python
ALL_VARIANTS = {
    # ...
    "CATEGORY/variant_name": {
        "name": "Description",
        "type": "category",
        "makefile": "spectre/CATEGORY/variant_name/Makefile",
        "binary": "spectre/CATEGORY/variant_name/variant_name"
    },
}
```

## 5. Test

```bash
make && ./variant_name
python3 run_all_tests.py --variants CATEGORY/variant_name --runs 5
```

## Platform-Specific Code

```c
#ifdef C910
    flush(&target);  // Hardware flush
#else
    evict_with_set(&evset);  // P550 eviction
#endif
```

## Troubleshooting

- **0% success**: Check threshold, add STALL(), verify disassembly
- **High FP**: Add page-aligned barriers, check probe order
- **Inconsistent**: Pin core with CORE=X, check thermal throttling
