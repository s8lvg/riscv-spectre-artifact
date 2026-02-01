#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <signal.h>
#include "../cacheutils.h"
#include "../libjit.h"

size_t __attribute__((aligned(4096))) probe[512 * 4096];
volatile uint64_t __attribute__((aligned(4096))) idx_operand = 100;
volatile uint64_t __attribute__((aligned(4096))) bound_operand = 10;

#ifdef CACHEUTILS_USE_EVICTION
eviction_set_t idx_operand_set;
eviction_set_t bound_operand_set;
eviction_set_t probe_set;
#endif

static jmp_buf trycatch_buf;

void unblock_signal(int signum) {
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, signum);
    sigprocmask(SIG_UNBLOCK, &sigs, NULL);
}

void trycatch_segfault_handler(int signum) {
    (void)signum;
    unblock_signal(SIGSEGV);
    longjmp(trycatch_buf, 1);
}

// OOP mistraining: 4K branches to pollute PHT
volatile int spray_cond = 1;
#define BEQ asm volatile("beq %0, %1, 1f\n1:" : : "r"(spray_cond), "r"(1));
#define BEQ_16 BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ BEQ
#define BEQ_256 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16 BEQ_16
#define BEQ_4K BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256 BEQ_256

void oop_mistrain(void) {
    if(spray_cond) spray_cond++;
    BEQ_4K
}

typedef void (*fault_fn_t)(void* probe_ptr);
typedef void (*branch_fn_t)(void* operand_ptr, void* probe_ptr);
typedef void (*branch_stall_bound_fn_t)(void* idx_ptr, void* bound_ptr, void* probe_ptr);

static inline int add_chain_size(int add_count) {
    return 4 + add_count * 4;
}

static inline void emit_add_chain(jit_buf_t* buf, int add_count) {
    jit_li(buf, T0, 0);
    for (int i = 0; i < add_count; i++) {
        jit_addi(buf, T0, T0, 1);
    }
}

// fault: null deref triggers speculation
jit_buf_t* generate_fault(int add_count) {
    jit_buf_t* buf = jit_init(4096);
    if (!buf) return NULL;

    jit_fence(buf);
    jit_lbu(buf, T0, ZERO, 0);
    emit_add_chain(buf, add_count);
    jit_lb(buf, T1, A0, 0);
    jit_ret(buf);

    jit_finalize(buf);
    return buf;
}

// branch_stall_idx: uncached(idx) >= IMM
jit_buf_t* generate_branch_stall_idx(int add_count) {
    jit_buf_t* buf = jit_init(4096);
    if (!buf) return NULL;

    jit_addi(buf, SP, SP, -16);
    jit_sd(buf, S1, SP, 0);
    jit_sd(buf, S2, SP, 8);

    jit_ld(buf, S1, A0, 0);
    jit_li(buf, S2, 10);

    int16_t skip_offset = add_chain_size(add_count) + 4 + 4;
    jit_bgeu(buf, S1, S2, skip_offset);

    emit_add_chain(buf, add_count);
    jit_ld(buf, T1, A1, 0);

    jit_ld(buf, S1, SP, 0);
    jit_ld(buf, S2, SP, 8);
    jit_addi(buf, SP, SP, 16);
    jit_ret(buf);

    jit_finalize(buf);
    return buf;
}

// branch_stall_bound: cached(idx) >= uncached(bound)
jit_buf_t* generate_branch_stall_bound(int add_count) {
    jit_buf_t* buf = jit_init(4096);
    if (!buf) return NULL;

    jit_addi(buf, SP, SP, -16);
    jit_sd(buf, S1, SP, 0);
    jit_sd(buf, S2, SP, 8);

    jit_ld(buf, S1, A0, 0);
    jit_ld(buf, S2, A1, 0);

    int16_t skip_offset = add_chain_size(add_count) + 4 + 4;
    jit_bgeu(buf, S1, S2, skip_offset);

    emit_add_chain(buf, add_count);
    jit_ld(buf, T1, A2, 0);

    jit_ld(buf, S1, SP, 0);
    jit_ld(buf, S2, SP, 8);
    jit_addi(buf, SP, SP, 16);
    jit_ret(buf);

    jit_finalize(buf);
    return buf;
}

// branch_imm: cached(idx) >= IMM
jit_buf_t* generate_branch_immediate(int add_count) {
    jit_buf_t* buf = jit_init(4096);
    if (!buf) return NULL;

    jit_addi(buf, SP, SP, -16);
    jit_sd(buf, S1, SP, 0);
    jit_sd(buf, S2, SP, 8);

    jit_ld(buf, S1, A0, 0);
    jit_li(buf, S2, 10);

    int16_t skip_offset = add_chain_size(add_count) + 4 + 4;
    jit_bgeu(buf, S1, S2, skip_offset);

    emit_add_chain(buf, add_count);
    jit_ld(buf, T1, A1, 0);

    jit_ld(buf, S1, SP, 0);
    jit_ld(buf, S2, SP, 8);
    jit_addi(buf, SP, SP, 16);
    jit_ret(buf);

    jit_finalize(buf);
    return buf;
}

typedef enum {
    TEST_FAULT,
    TEST_BRANCH_STALL_IDX,
    TEST_BRANCH_STALL_BOUND,
    TEST_BRANCH_IMMEDIATE,
} test_type_t;

typedef struct {
    const char* name;
    test_type_t type;
} testcase_t;

testcase_t testcases[] = {
    {"fault",              TEST_FAULT},
    {"branch_imm",         TEST_BRANCH_IMMEDIATE},
    {"branch_stall_bound", TEST_BRANCH_STALL_BOUND},
    {"branch_stall_idx",   TEST_BRANCH_STALL_IDX},
};

#define NUM_TESTCASES (sizeof(testcases) / sizeof(testcases[0]))

void run_testcase(testcase_t* tc, int max_nops, int step, int trials) {
    char filename[256];
    snprintf(filename, sizeof(filename), "framework_%s.csv", tc->name);
    FILE* csv = fopen(filename, "w");
    if (!csv) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return;
    }

    printf("=== %s ===\n", tc->name);
    fprintf(csv, "adds,min_time,avg_time\n");

    size_t* min_timings = malloc((max_nops + 1) * sizeof(size_t));
    size_t* avg_timings = malloc((max_nops + 1) * sizeof(size_t));

    for (int nops = 0; nops <= max_nops; nops += step) {
        size_t min_time = ~0ULL;
        size_t sum_time = 0;

        jit_buf_t* buf = NULL;
        switch (tc->type) {
            case TEST_FAULT:
                buf = generate_fault(nops);
                break;
            case TEST_BRANCH_STALL_IDX:
                buf = generate_branch_stall_idx(nops);
                break;
            case TEST_BRANCH_STALL_BOUND:
                buf = generate_branch_stall_bound(nops);
                break;
            case TEST_BRANCH_IMMEDIATE:
                buf = generate_branch_immediate(nops);
                break;
        }
        if (!buf) continue;
        void* fn = buf->code;
        char* probe_ptr = (char*)probe;

        // Warmup: train branch predictor for not-taken
        for (int w = 0; w < 10; w++) {
            flush(probe_ptr);
            mfence();
            switch (tc->type) {
                case TEST_FAULT:
                    if (!setjmp(trycatch_buf)) {
                        ((fault_fn_t)fn)(probe_ptr);
                    }
                    break;
                case TEST_BRANCH_STALL_IDX:
                case TEST_BRANCH_IMMEDIATE:
                    idx_operand = 5;
                    ((branch_fn_t)fn)((void*)&idx_operand, probe_ptr);
                    break;
                case TEST_BRANCH_STALL_BOUND:
                    idx_operand = 100;
                    bound_operand = 200;
                    ((branch_stall_bound_fn_t)fn)((void*)&idx_operand, (void*)&bound_operand, probe_ptr);
                    break;
            }
        }

        for (int m = 0; m < trials; m++) {
            switch (tc->type) {
                case TEST_FAULT:
#ifdef CACHEUTILS_USE_EVICTION
                    evict_with_set(&probe_set);
#else
                    flush(probe_ptr);
#endif
                    mfence();
                    if (!setjmp(trycatch_buf)) {
                        ((fault_fn_t)fn)(probe_ptr);
                    }
                    break;

                case TEST_BRANCH_STALL_IDX:
                    for (int i = 0; i < 10; i++) oop_mistrain();
                    idx_operand = 100;
#ifdef CACHEUTILS_USE_EVICTION
                    evict_with_set(&probe_set);
                    evict_with_set(&idx_operand_set);
#else
                    flush(probe_ptr);
                    flush((void*)&idx_operand);
#endif
                    mfence();
                    ((branch_fn_t)fn)((void*)&idx_operand, probe_ptr);
                    break;

                case TEST_BRANCH_STALL_BOUND:
                    for (int i = 0; i < 10; i++) oop_mistrain();
                    idx_operand = 100;
                    bound_operand = 10;
#ifdef CACHEUTILS_USE_EVICTION
                    evict_with_set(&probe_set);
                    evict_with_set(&bound_operand_set);
#else
                    flush(probe_ptr);
                    flush((void*)&bound_operand);
#endif
                    mfence();
                    maccess((void*)&idx_operand);
                    ((branch_stall_bound_fn_t)fn)((void*)&idx_operand, (void*)&bound_operand, probe_ptr);
                    break;

                case TEST_BRANCH_IMMEDIATE:
                    for (int i = 0; i < 10; i++) oop_mistrain();
                    idx_operand = 100;
#ifdef CACHEUTILS_USE_EVICTION
                    evict_with_set(&probe_set);
#else
                    flush(probe_ptr);
#endif
                    mfence();
                    maccess((void*)&idx_operand);
                    ((branch_fn_t)fn)((void*)&idx_operand, probe_ptr);
                    break;
            }
            mfence();

            size_t t = reload_t(probe_ptr);
            if (t < min_time) min_time = t;
            sum_time += t;
        }

        size_t avg_time = sum_time / trials;
        min_timings[nops] = min_time;
        avg_timings[nops] = avg_time;
        printf("adds=%3d: min=%3zu avg=%3zu\n", nops, min_time, avg_time);
        fprintf(csv, "%d,%zu,%zu\n", nops, min_time, avg_time);
        fflush(csv);

        jit_free(buf);
    }

    // Detect window: threshold = (miss + 2*hit) / 3
    size_t hit_avg = 0, miss_avg = 0;
    for (int i = 0; i < 5; i++) {
        hit_avg += min_timings[i * step];
        miss_avg += min_timings[max_nops - (i + 1) * step];
    }
    hit_avg /= 5;
    miss_avg /= 5;
    size_t detect_threshold = (miss_avg + hit_avg * 2) / 3;

    for (int nops = max_nops - step; nops >= 0; nops -= step) {
        if (min_timings[nops] < detect_threshold) {
            printf("==> Window: %d instructions (threshold=%zu, hit_avg=%zu, miss_avg=%zu)\n\n",
                   nops, detect_threshold, hit_avg, miss_avg);
            break;
        }
    }

    fclose(csv);
    free(min_timings);
    free(avg_timings);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("=== Speculation Window Test ===\n\n");

    pin_to_core(0);
    cacheutils_init();
    signal(SIGSEGV, trycatch_segfault_handler);

    int max_nops = 200;
    int step = 1;
    int trials = 1000;

#ifdef CACHEUTILS_USE_EVICTION
    printf("Building eviction sets for P550...\n");

    for (size_t i = 0; i < sizeof(probe); i += 4096) {
        ((volatile char*)probe)[i] = 0xAA;
    }

    if (build_eviction_set_vaddr((void*)&idx_operand, &idx_operand_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for idx_operand\n");
        return 1;
    }
    if (build_eviction_set_vaddr((void*)&bound_operand, &bound_operand_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for bound_operand\n");
        return 1;
    }

    if (build_eviction_set_vaddr((void*)probe, &probe_set) != 0) {
        fprintf(stderr, "Failed to build eviction set for probe\n");
        return 1;
    }
    printf("Built eviction sets\n\n");
#endif

    for (size_t i = 0; i < NUM_TESTCASES; i++) {
        run_testcase(&testcases[i], max_nops, step, trials);
    }

    cacheutils_cleanup();

    return 0;
}
