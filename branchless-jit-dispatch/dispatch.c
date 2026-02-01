/*
 * Branchless JIT dispatch benchmark for RISC-V
 * Measures overhead of Spectre-BTB mitigation using direct branches.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <sys/mman.h>

size_t CACHE_MISS = 0;
#include "../cacheutils.h"

#define NUM_WARMUP 100000
#define NUM_TRIALS 100000

/*
* Encode a JAL instruction to jump to 'offset' from the current PC,
* writing the return address to register 'rd'.
* This whole function is branchfree and therefore spectre-safe. 
*/
static inline uint32_t encode_jal(int32_t offset, int rd) {
    return 0x6F
         | ((rd & 0x1F) << 7)
         | (((offset >> 12) & 0xFF) << 12)
         | (((offset >> 11) & 0x1) << 20)
         | (((offset >> 1) & 0x3FF) << 21)
         | (((offset >> 20) & 0x1) << 31);
}

/*
 * Jit slot we jump to. 
 * Contains one instruction that is patched at runtime to jump to the target,
 * Next instruction jumps back to dispatch epilogue.
*/
__attribute__((naked, section(".text.jit"), aligned(4)))
void __jit_region(void) {
    __asm__ volatile(
        ".global jit_slot\n"
        "jit_slot:\n"
        ".word 0x00100073\n"
        ".word 0x00100073\n"
    );
}

extern uint32_t jit_slot[];
extern char dispatch_epilogue[];

static int init_jit(void) {
    uintptr_t page = (uintptr_t)jit_slot & ~0xFFFUL;
    if (mprotect((void*)page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC))
        return -1;
    jit_slot[1] = encode_jal(dispatch_epilogue - (char*)&jit_slot[1], 0);
    return 0;
}

__attribute__((noinline))
void dispatch(void) {
    register void *target __asm__("t0");
    __asm__ volatile("" : "=r"(target));
    jit_slot[0] = encode_jal((char*)target - (char*)&jit_slot[0], 1);
    __asm__ volatile(
        "fence.i\n"
        "j jit_slot\n"
        ".global dispatch_epilogue\n"
        "dispatch_epilogue:\n"
        ::: "ra", "memory"
    );
}

#define CALL(ret_t, fn, ...) ({                        \
    register void *_t0 __asm__("t0") = (void*)(fn);     \
    __asm__ volatile("" :: "r"(_t0));                   \
    ((ret_t (*)(int))dispatch)(__VA_ARGS__);            \
})

typedef int (*target_fn)(int);
__attribute__((noinline)) int target0(int x) { return x + 0; }
__attribute__((noinline)) int target1(int x) { return x + 1; }
__attribute__((noinline)) int target2(int x) { return x + 2; }
__attribute__((noinline)) int target3(int x) { return x + 3; }

__attribute__((noinline))
int indirect_call(target_fn fn, int arg) { return fn(arg); }

static int cmp_u64(const void *a, const void *b) {
    uint64_t ua = *(const uint64_t *)a, ub = *(const uint64_t *)b;
    return (ua > ub) - (ua < ub);
}

static void stats(uint64_t *arr, int n, double *med, double *std) {
    qsort(arr, n, sizeof(uint64_t), cmp_u64);

    /* Trim top/bottom 1% to remove interrupt outliers */
    int lo = n / 100, hi = n - n / 100;
    arr += lo;
    n = hi - lo;

    *med = arr[n/2];
    double sum = 0;
    for (int i = 0; i < n; i++) sum += arr[i];
    double avg = sum / n;
    double var = 0;
    for (int i = 0; i < n; i++) var += (arr[i] - avg) * (arr[i] - avg);
    *std = sqrt(var / (n - 1));
}

int main(void) {
    if (cacheutils_init() < 0 || init_jit() < 0) return 1;

    target_fn targets[4] = {target0, target1, target2, target3};
    uint64_t *ind_t = malloc(NUM_TRIALS * sizeof(uint64_t));
    uint64_t *jit_t = malloc(NUM_TRIALS * sizeof(uint64_t));

    /* Warmup */
    for (int i = 0; i < NUM_WARMUP; i++) {
        volatile int r = indirect_call(targets[i % 4], i);
        r = CALL(int, targets[i % 4], i);
        NO_OPT(r);
    }

    /* Measure indirect calls */
    for (int i = 0; i < NUM_TRIALS; i++) {
        uint64_t t0 = rdtsc();
        volatile int r = indirect_call(targets[i % 4], i);
        ind_t[i] = rdtsc() - t0;
        NO_OPT(r);
    }

    /* Measure JIT dispatch via CALL macro */
    for (int i = 0; i < NUM_TRIALS; i++) {
        uint64_t t0 = rdtsc();
        volatile int r = CALL(int, targets[i % 4], i);
        jit_t[i] = rdtsc() - t0;
        NO_OPT(r);
    }

    double ind_med, ind_std, jit_med, jit_std;
    stats(ind_t, NUM_TRIALS, &ind_med, &ind_std);
    stats(jit_t, NUM_TRIALS, &jit_med, &jit_std);

    printf("Indirect call:  %.0f cycles (std=%.1f)\n", ind_med, ind_std);
    printf("JIT dispatch:   %.0f cycles (std=%.1f)\n", jit_med, jit_std);
    printf("Overhead:       %.1fx\n", jit_med / ind_med);

    free(ind_t); free(jit_t);
    cacheutils_cleanup();
    return 0;
}
