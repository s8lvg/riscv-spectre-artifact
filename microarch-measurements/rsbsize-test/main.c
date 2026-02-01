#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

size_t CACHE_MISS = 0;
#include "../cacheutils.h"
#include "../libjit.h"

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

/*
 * Measure a single depth from inside the RSB-filling recursion.
 * Called at the bottom of the recursion where RSB is in known state.
 */
static __attribute__((noinline)) uint64_t measure_single(int depth) {
    void (*fn)(void) = (void(*)(void))g_funcs[depth];
    uint64_t min_time = UINT64_MAX;

    /* Warmup */
    for (size_t i = 0; i < WARMUP; i++) {
        fn();
    }

    /* Measure minimum time */
    for (size_t i = 0; i < RUNS; i++) {
        uint64_t start = rdtsc();
        fn();
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

    /* Generate RSB test chain */
    g_funcs = jit_rsb_chain(buf, MAX_DEPTH);
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
