/**
 * Barrier Test: Tests whether barriers stop straight-line speculation after faults.
 * Uses JIT code generation and Flush+Reload to detect speculative execution.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>

size_t CACHE_MISS = 0;
#include "../cacheutils.h"
#include "../libjit.h"

#define ITERATIONS 10000
#define NUM_LOADS 8

static char __attribute__((aligned(4096))) probe_array[4096 * 2];
static char __attribute__((aligned(8))) dummy_atomic[8];

#ifdef CACHEUTILS_USE_EVICTION
static eviction_set_t probe_evset;
#endif

static sigjmp_buf jump_buf;

static void fault_handler(int sig) {
    (void)sig;
    siglongjmp(jump_buf, 1);
}

/* ============================================================================
 * Fault triggers
 * ============================================================================ */

typedef enum {
    F_LD_NULL,      /* Load access fault: load from address 0 */
    F_ST_NULL,      /* Store access fault: store to address 0 */
    F_LD_UNMAP,     /* Load page fault: load from unmapped address */
    F_ST_UNMAP,     /* Store page fault: store to unmapped address */
    F_JMP_UNMAP,    /* Instruction page fault: jump to unmapped address */
    F_UNIMP,        /* Illegal instruction */
    F_EBREAK,       /* Breakpoint */
    F_COUNT
} fault_t;

static const char* fault_names[] = {
    "ld_null", "st_null", "ld_unmap", "st_unmap", "jmp_unmap", "unimp", "ebreak"
};

static void emit_fault(jit_buf_t* buf, fault_t f) {
    switch (f) {
    case F_LD_NULL:   jit_ld(buf, ZERO, ZERO, 0); break;
    case F_ST_NULL:   jit_sd(buf, ZERO, ZERO, 0); break;
    case F_LD_UNMAP:  /* Load from 0xDEAD0000 (unmapped) */
        jit_lui(buf, T1, 0xDEAD0);
        jit_ld(buf, T0, T1, 0);
        break;
    case F_ST_UNMAP:  /* Store to 0xDEAD0000 (unmapped) */
        jit_lui(buf, T1, 0xDEAD0);
        jit_sd(buf, ZERO, T1, 0);
        break;
    case F_JMP_UNMAP: /* Jump to 0xDEAD0000 (unmapped) */
        jit_lui(buf, T1, 0xDEAD0);
        jit_jalr(buf, ZERO, T1, 0);
        break;
    case F_UNIMP:     jit_emit(buf, 0x00000000); break;
    case F_EBREAK:    jit_ebreak(buf); break;
    default: break;
    }
}

/* ============================================================================
 * Barrier definitions
 * ============================================================================ */

typedef enum {
    B_NONE, B_FENCE_IORW, B_FENCE_RW, B_FENCE_R, B_FENCE_W, B_FENCE_TSO,
    B_FENCE_I, B_PAUSE, B_RDCYCLE, B_RDTIME, B_RDINSTRET, B_LR_D,
    B_COUNT
} barrier_t;

static const char* barrier_names[] = {
    "none", "fence_iorw", "fence_rw", "fence_r", "fence_w", "fence_tso",
    "fence_i", "pause", "rdcycle", "rdtime", "rdinstret", "lr_d"
};

static void emit_barrier(jit_buf_t* buf, barrier_t b) {
    switch (b) {
    case B_NONE: break;
    case B_FENCE_IORW: jit_fence(buf); break;
    case B_FENCE_RW: jit_fence_rw_rw(buf); break;
    case B_FENCE_R: jit_fence_r_r(buf); break;
    case B_FENCE_TSO: jit_fence_tso(buf); break;
    case B_FENCE_I: jit_fence_i(buf); break;
    case B_PAUSE: jit_pause(buf); break;
    case B_RDCYCLE: jit_rdcycle(buf, T1); break;
    case B_RDTIME: jit_rdtime(buf, T1); break;
    case B_RDINSTRET: jit_rdinstret(buf, T1); break;
    case B_LR_D: jit_lr_d(buf, T2, S4); break;
    case B_FENCE_W: jit_fence_w_w(buf); break;
    default: break;
    }
}

static void* generate_test(jit_buf_t* buf, fault_t fault, barrier_t barrier) {
    void* entry = jit_get_pc(buf);
    jit_addi(buf, S2, A0, 0);
    jit_li(buf, S4, (uint64_t)dummy_atomic);
    /* Fault trigger */
    emit_fault(buf, fault);
    /* Barrier under test */
    emit_barrier(buf, barrier);
    /* Speculative loads */
    for (int i = 0; i < NUM_LOADS; i++)
        jit_lb(buf, T0, S2, 0);
    jit_ret(buf);
    return entry;
}

/* ============================================================================
 * Test runners
 * ============================================================================ */

static int run_user_test(fault_t fault, barrier_t barrier, size_t threshold) {
    jit_buf_t* buf = jit_init(4096);
    if (!buf) return -1;

    void* entry = generate_test(buf, fault, barrier);
    jit_finalize(buf);

    struct sigaction sa, old_segv, old_ill, old_trap, old_bus;
    sa.sa_handler = fault_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGILL, &sa, &old_ill);
    sigaction(SIGTRAP, &sa, &old_trap);
    sigaction(SIGBUS, &sa, &old_bus);

    void (*fn)(char*) = (void (*)(char*))entry;
    int hits = 0;

    for (int i = 0; i < ITERATIONS; i++) {
#ifdef CACHEUTILS_USE_EVICTION
        evict_with_set(&probe_evset);
#else
        flush(probe_array);
#endif
        mfence();
        if (sigsetjmp(jump_buf, 1) == 0)
            fn(probe_array);
        if (reload_t(probe_array) < (int)threshold)
            hits++;
    }

    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGILL, &old_ill, NULL);
    sigaction(SIGTRAP, &old_trap, NULL);
    sigaction(SIGBUS, &old_bus, NULL);
    jit_free(buf);
    return hits;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    memset(probe_array, 0xff, sizeof(probe_array));
    memset(dummy_atomic, 0, sizeof(dummy_atomic));

    if (cacheutils_init() < 0) {
        fprintf(stderr, "cacheutils init failed\n");
        return 1;
    }

    size_t threshold = detect_flush_reload_threshold();
    printf("Threshold: %zu cycles\n", threshold);

#ifdef CACHEUTILS_USE_EVICTION
    if (build_eviction_set_vaddr(probe_array, &probe_evset) != 0) {
        fprintf(stderr, "eviction set failed\n");
        cacheutils_cleanup();
        return 1;
    }
    printf("Mode: eviction (P550)\n");
#else
    printf("Mode: flush (C910)\n");
#endif

    /* Print header */
    printf("fault,barrier,hits\n");

    /* Test all fault × barrier combinations */
    for (fault_t f = 0; f < F_COUNT; f++) {
        for (barrier_t b = 0; b < B_COUNT; b++) {
            int hits = run_user_test(f, b, threshold);
            printf("%s,%s,%.1f\n", fault_names[f], barrier_names[b],
                   100.0 * hits / ITERATIONS);
            fflush(stdout);
        }
    }

    cacheutils_cleanup();
    return 0;
}
