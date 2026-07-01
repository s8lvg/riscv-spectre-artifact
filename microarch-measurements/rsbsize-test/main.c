#define _GNU_SOURCE

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

size_t CACHE_MISS = 0;
#include "../../cacheutils.h"
#include "../../libjit.h"

#define MAX_DEPTH 40
#define RUNS 1000000
#define WARMUP 10000

/*
 * RSB_FILL_DEPTH: number of recursive calls before measurement.
 * This ensures the RSB is in a consistent (full) state before we start,
 * eliminating variability from OS/libc startup call depth.
 */
#define RSB_FILL_DEPTH 50

uint64_t results[MAX_DEPTH] = {0};

/* Global pointers for use in recursive measurement function */
static void** g_funcs;

static void** jit_rsb_return_timestamp_chain(jit_buf_t* buf, size_t depth) {
    void** funcs = malloc(depth * sizeof(void*));
    if (!funcs) {
        return NULL;
    }

    for (size_t i = 0; i < depth; i++) {
        funcs[i] = jit_get_pc(buf);

        if (i == 0) {
            /*
             * Start timing immediately before the nested returns unwind.
             * The cycle value is returned in a0 to the C harness.
             */
            jit_fence_rw_rw(buf);
            jit_rdcycle(buf, A0);
            jit_fence_rw_rw(buf);
            jit_ret(buf);
        } else {
            /* Save/restore architectural ra; the hardware RAS is populated by jal. */
            jit_addi(buf, SP, SP, -16);
            jit_sd(buf, RA, SP, 0);

            void* current_pc = jit_get_pc(buf);
            int32_t offset = (int32_t)((char*)funcs[i - 1] - (char*)current_pc);
            jit_jal(buf, RA, offset);

            jit_ld(buf, RA, SP, 0);
            jit_addi(buf, SP, SP, 16);
            jit_ret(buf);
        }
    }

    jit_finalize(buf);
    return funcs;
}

/*
 * Measure only the return side of the generated call chain.
 *
 * The generated leaf reads rdcycle into a0. The timed interval therefore starts
 * after all direct calls have already executed and just before the nested
 * returns begin. The harness records the end timestamp after the outer return.
 */
static __attribute__((noinline)) uint64_t measure_single(int depth) {
    uint64_t (*fn)(void) = (uint64_t(*)(void))g_funcs[depth];
    uint64_t min_time = UINT64_MAX;

    /* Warmup */
    for (size_t i = 0; i < WARMUP; i++) {
        NO_OPT(fn());
    }

    /* Measure minimum time */
    for (size_t i = 0; i < RUNS; i++) {
        uint64_t start = fn();
        uint64_t end = rdtsc();

        uint64_t delta = end - start;
        if (delta < min_time) {
            min_time = delta;
        }
    }
    return min_time;
}

/*
 * Recursive function to fill RSB before measurement.
 * At the bottom of recursion (fill_depth == 0), the RSB contains
 * RSB_FILL_DEPTH entries, providing a consistent starting state.
 */
static __attribute__((noinline)) void measure_with_rsb_filled(int fill_depth, int measure_depth) {
    if (fill_depth > 0) {
        measure_with_rsb_filled(fill_depth - 1, measure_depth);
        return;
    }

    /* At bottom: RSB is now filled with RSB_FILL_DEPTH entries */
    results[measure_depth] = measure_single(measure_depth);
}

int main(void){
    /* Initialize timing infrastructure */
    if (cacheutils_init() < 0) {
        fprintf(stderr, "Failed to initialize cacheutils\n");
        return 1;
    }

    /* Allocate JIT buffer */
    jit_buf_t* buf = jit_init(PAGE_SIZE * 10);
    if (!buf) {
        fprintf(stderr, "Failed to allocate JIT buffer\n");
        return 1;
    }

    /* Generate RSB test chain with leaf-side timestamping */
    g_funcs = jit_rsb_return_timestamp_chain(buf, MAX_DEPTH);
    if (!g_funcs) {
        fprintf(stderr, "Failed to generate RSB chain\n");
        jit_free(buf);
        return 1;
    }

    printf("Generated %d nested functions\n", MAX_DEPTH);
    printf("RSB fill depth: %d (ensures consistent RSB state)\n", RSB_FILL_DEPTH);
    printf("Warmup: %d iterations, Measurement: %d iterations per depth\n", WARMUP, RUNS);

    /*
     * Benchmark each depth with RSB pre-filled.
     * Each measurement starts from a consistent RSB state.
     */
    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        measure_with_rsb_filled(RSB_FILL_DEPTH, depth);
    }

    /* Print results with deltas */
    printf("\nDepth | Cycles | Delta\n");
    printf("------+--------+------\n");
    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        uint64_t cycles = results[depth];
        if (depth == 0) {
            printf("%5d | %6lu | %5s\n", depth, cycles, "-");
        } else {
            int64_t d = (int64_t)(cycles - results[depth-1]);
            printf("%5d | %6lu | %+5ld\n", depth, cycles, d);
        }
    }

    /* Write results to log file */
    FILE* log = fopen("log.txt", "w");
    for (int i = 0; i < MAX_DEPTH; i++) {
        fprintf(log, "%d,%lu\n", i, results[i]);
    }
    fclose(log);

    /* Write deltas to separate file for step visualization */
    FILE* delta_log = fopen("log_delta.txt", "w");
    for (int i = 1; i < MAX_DEPTH; i++) {
        int64_t delta = (int64_t)(results[i] - results[i-1]);
        fprintf(delta_log, "%d,%ld\n", i, delta);
    }
    fclose(delta_log);

    /* Cleanup */
    free(g_funcs);
    jit_free(buf);
    cacheutils_cleanup();

    printf("\nResults written to log.txt\n");
    return 0;
}
