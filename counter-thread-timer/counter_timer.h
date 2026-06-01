/*
 * Counter-thread timer for RISC-V.
 *
 * Provides a high-resolution timing source that does not depend on the
 * rdcycle CSR (which recent kernels disable in userspace). A background
 * thread increments a shared, cache-line aligned counter in a tight loop.
 * The main thread reads the counter as a monotonic clock.
 *
 * Restored from git history (experiments/cacheutils.h @ 8f8ec99a^,
 * experiments/p550-timer/counter_thread.c @ 4453974c).
 *
 * Usage:
 *   counter_timer_start();          // spawn + pin counter thread
 *   uint64_t t0 = ctr_read();
 *   ... work ...
 *   uint64_t t1 = ctr_read();
 *   counter_timer_stop();
 *
 * Counter core is configurable via the COUNTER_CORE env var (default 1).
 */
#ifndef COUNTER_TIMER_H
#define COUNTER_TIMER_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

/* Cache-line aligned counter to prevent false sharing. */
typedef struct {
    _Alignas(64) volatile uint64_t value;
    char pad[56]; /* pad to 64 bytes */
} counter_timer_t;

static counter_timer_t *g_counter = NULL;
static pthread_t g_counter_thread;
static int g_counter_running = 0;

__attribute__((noreturn))
static void* counter_timer_loop(void* arg) {
    counter_timer_t *counter = (counter_timer_t*)arg;
    __asm__ __volatile__(
        "   li      t0, 0\n"   /* initialize counter to 0          */
        "1:\n"                  /* loop label                       */
        "   addi    t0, t0, 1\n"/* increment local register        */
        "   sd      t0, (%0)\n" /* store to memory (non-atomic)     */
        "   nop\n"             /* delay: 1 NOP for optimal resolution */
        "   j       1b\n"      /* jump to start of loop            */
        :: "r"(&counter->value)
        : "t0", "memory"
    );
    __builtin_unreachable();
}

/* Read the counter with surrounding fences for serialization. */
__attribute__((always_inline))
static inline uint64_t ctr_read(void) {
    uint64_t val;
    __asm__ __volatile__(
        "fence rw,rw\n"
        "lr.d %0, (%1)\n"
        "fence rw,rw\n"
        : "=r"(val)
        : "r"(&g_counter->value)
        : "memory"
    );
    return val;
}

static int counter_timer_start(void) {
    if (g_counter_running) {
        return 0;
    }

    g_counter = aligned_alloc(64, sizeof(counter_timer_t));
    if (!g_counter) {
        return -1;
    }
    g_counter->value = 0;
    g_counter_running = 1;

    int ret = pthread_create(&g_counter_thread, NULL,
                             counter_timer_loop, (void*)g_counter);
    if (ret != 0) {
        free(g_counter);
        g_counter = NULL;
        g_counter_running = 0;
        return -1;
    }

    /* Pin counter thread to a dedicated core (default 1, override via
     * COUNTER_CORE). This prevents cache-line contention between the
     * measurement thread and the counter thread. */
    char* counter_core_env = getenv("COUNTER_CORE");
    int counter_core = counter_core_env ? atoi(counter_core_env) : 1;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(counter_core, &cpuset);
    pthread_setaffinity_np(g_counter_thread, sizeof(cpu_set_t), &cpuset);

    /* Give the thread time to start. */
    usleep(10000);
    return 0;
}

static void counter_timer_stop(void) {
    if (g_counter_running) {
        pthread_cancel(g_counter_thread);
        g_counter_running = 0;
        if (g_counter) {
            free(g_counter);
            g_counter = NULL;
        }
    }
}

#endif /* COUNTER_TIMER_H */
