/**
 * Baseline Cache Encoding Test
 *
 * Tests basic cache encoding/decoding without any speculation.
 * This verifies that eviction sets and timing work correctly.
 *
 * Expected result: 100% accuracy (or very close)
 */

#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../exploit_framework.h"

// Simple cache encoding test
void baseline_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    // Encode secret into cache by accessing probe_array[encode_index]
    maccess(ctx->probe_array + encode_index * PAGE_SIZE);
    mfence();
}

int main() {
    exploit_ctx_t ctx = {0};

    // Initialize framework
    if (exploit_init(&ctx) != 0) {
        return 1;
    }

    printf("Baseline Cache Encoding Test (dynamic values per iteration)\n");

    // Setup hooks (minimal - no setup needed)
    exploit_hooks_t hooks = {
        .setup = NULL,
        .attack = baseline_attack,
        .cleanup = NULL
    };

    // Run test
    exploit_run_simple(&ctx, &hooks, NULL);

    // Print results
    exploit_print_results("Baseline", &ctx);

    return 0;
}
