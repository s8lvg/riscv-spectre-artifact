/*
 * Counter-thread timer resolution benchmark.
 *
 * Measures the resolution of the counter-thread timer (counter_timer.h)
 * and reports it both in counter ticks and in CPU cycles. Backs the paper
 * claim of ~6 cycle resolution on the T-Head C910.
 *
 * Method:
 *   1. Spawn the counter thread (pinned to COUNTER_CORE, default 1).
 *   2. Calibrate cycles-per-tick by reading rdcycle and the counter at two
 *      points separated by a busy wait.
 *   3. Read the counter twice back-to-back many times; the minimum nonzero
 *      delta is the timer resolution in ticks. Multiply by cycles-per-tick
 *      to express it in cycles.
 *
 * rdcycle is used only for calibration. On C910 it is readable via CSR.
 * On P550 with recent kernels, rdcycle must be enabled (perf); pass
 * -DNO_RDCYCLE to skip cycle calibration and report ticks only.
 *
 * Pin the main thread to a different core than the counter, e.g.:
 *   COUNTER_CORE=1 taskset -c 0 ./bench
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "counter_timer.h"

#define SAMPLES   1000000
#define CAL_LOOPS 2000000

#ifndef NO_RDCYCLE
__attribute__((always_inline))
static inline uint64_t rdcycle(void) {
    uint64_t v;
    __asm__ __volatile__("fence rw,rw\n rdcycle %0\n fence rw,rw\n" : "=r"(v));
    return v;
}
#endif

int main(void) {
    if (counter_timer_start() != 0) {
        fprintf(stderr, "failed to start counter thread\n");
        return 1;
    }

    /* Wait until the counter is actually advancing. */
    uint64_t a = ctr_read();
    for (int i = 0; i < 100000000 && ctr_read() == a; i++) { }
    if (ctr_read() == a) {
        fprintf(stderr, "counter not advancing (check COUNTER_CORE / cores)\n");
        counter_timer_stop();
        return 1;
    }

    double cycles_per_tick = 0.0;
#ifndef NO_RDCYCLE
    /* Calibrate cycles-per-tick. */
    uint64_t c0 = rdcycle(), t0 = ctr_read();
    for (volatile uint64_t i = 0; i < CAL_LOOPS; i++) { }
    uint64_t c1 = rdcycle(), t1 = ctr_read();
    uint64_t dcyc = c1 - c0, dtick = t1 - t0;
    if (dtick == 0) {
        fprintf(stderr, "calibration failed: counter did not advance\n");
        counter_timer_stop();
        return 1;
    }
    cycles_per_tick = (double)dcyc / (double)dtick;
    printf("calibration: %llu cycles over %llu ticks => %.3f cycles/tick\n",
           (unsigned long long)dcyc, (unsigned long long)dtick, cycles_per_tick);
#else
    printf("calibration: skipped (NO_RDCYCLE), reporting ticks only\n");
#endif

    /* Measure resolution: minimum nonzero delta between back-to-back reads. */
    uint64_t min_delta = UINT64_MAX;
    uint64_t zero = 0, nonzero = 0;
    for (int i = 0; i < SAMPLES; i++) {
        uint64_t s = ctr_read();
        uint64_t e = ctr_read();
        uint64_t d = e - s;
        if (d == 0) { zero++; continue; }
        nonzero++;
        if (d < min_delta) min_delta = d;
    }

    printf("samples: %d (nonzero deltas: %llu, zero deltas: %llu)\n",
           SAMPLES, (unsigned long long)nonzero, (unsigned long long)zero);
    printf("resolution: %llu ticks\n", (unsigned long long)min_delta);
#ifndef NO_RDCYCLE
    printf("resolution: %.2f cycles\n", (double)min_delta * cycles_per_tick);
#endif

    counter_timer_stop();
    return 0;
}
