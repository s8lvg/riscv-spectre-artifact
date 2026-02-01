#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "../cacheutils.h"

#define NUM_SAMPLES 0x10000
#define countof(x) (sizeof(x) / sizeof(*(x)))

// Fence instructions
asm (
    ".globl f_rwrw\n"
    "f_rwrw:\n"
    "fence rw,rw\n"

    ".globl f_rr\n"
    "f_rr:\n"
    "fence r,r\n"

    ".globl f_ww\n"
    "f_ww:\n"
    "fence w,w\n"

    ".globl f_rw\n"
    "f_rw:\n"
    "fence r,w\n"

    ".globl f_wr\n"
    "f_wr:\n"
    "fence w,r\n"

    ".globl f_i\n"
    "f_i:\n"
    "fence.i\n"

    ".globl retpoline\n" // Retpoline speculation trap
    "retpoline:\n"
    "jal 1f\n"
    "j .\n"
    "1:\n"
    "la ra, 2f\n"
    "ret\n"
    "2:\n"
    ".globl retploline_end\nretpoline_end:\nnop"
);

#ifdef P550
#define MFENCE asm volatile ("fence.i\nfence rw,rw\n")
#else
#define MFENCE mfence()
#endif

static unsigned char __attribute__((aligned(0x1000))) test_buf[0x1000] = {0, };
static unsigned long long times[NUM_SAMPLES] = {0, };

extern void test_function_code_end();
extern void fence_dest();
extern void ret_dest();
extern void retpoline();
extern void retpoline_end();

extern uint32_t f_rwrw;
extern uint32_t f_rr;
extern uint32_t f_ww;
extern uint32_t f_rw;
extern uint32_t f_wr;
extern uint32_t f_i;

void test_function_code(unsigned long long* value) {
    asm volatile (
        // Prologue
        "addi sp, sp, -16\n"
        "sd ra, 8(sp)\n"

        // Call
        "jal 999f\n"

        // Speculation starts here
        ".globl fence_dest\n"
        "fence_dest:\n"

        // Leave some space for jitting
        // 64 bytes (16 nops): Space for fence
        // 32 bytes (8 nops):  Move address of test_buf into a0 from immediates
        ".rept 24\n"
            "nop\n"
        ".endr\n"

        // JIT code loads the address of a test buffer from immediates into a0 - access here
        "ld a0, (a0)\n"
        "j .\n" // Infinite loop - speculation ends here


        // Call target - confuse RSB by overwriting ra from flushed pointer - give pointer to ret_dest as arguments
        "999:\n"
        "ld ra, (%0)\n"
        "ret\n"

        // Architectural execution continues here
        ".globl ret_dest\n"
        "ret_dest:\n"

        // Epilogue
        "ld ra,8(sp)\n"
        "addi sp,sp,16\n"
        "ret\n"

        ".globl test_function_code_end\n"
        "test_function_code_end:\n"
    :: "r"(value)
    : "memory"
    );
}

// Fill buffer with RISC-V instructions to load a 64-bit immediate into a0
void fill_load_immediate(uint32_t* buf, uint64_t value) {
    // Load 64-bit immediate requires multiple instructions:
    // 1. lui a0, upper_20bits    - load upper immediate
    // 2. addi a0, a0, lower_12bits - add lower bits
    // 3. For full 64-bit: slli + addi + slli + addi pattern

    // Extract parts of the 64-bit value
    uint32_t imm_63_44 = (value >> 44) & 0xFFFFF;
    uint32_t imm_43_32 = (value >> 32) & 0xFFF;
    uint32_t imm_31_12 = (value >> 12) & 0xFFFFF;
    uint32_t imm_11_0  = value & 0xFFF;

    int idx = 0;

    // Build from top down: load upper 20 bits of top 32 bits
    buf[idx++] = 0x00000537 | (imm_63_44 << 12); // lui a0, imm_63_44

    // Add next 12 bits
    if (imm_43_32 != 0 || (imm_43_32 & 0x800)) {
        buf[idx++] = 0x00050513 | (imm_43_32 << 20); // addi a0, a0, imm_43_32
    }

    // Shift left 32 bits
    buf[idx++] = 0x02051513; // slli a0, a0, 32

    // Load lower 32 bits - upper 20
    if (imm_31_12 != 0) {
        buf[idx++] = 0x000005b7 | (imm_31_12 << 12); // lui a1, imm_31_12
        buf[idx++] = 0x02059593; // slli a1, a1, 32
        buf[idx++] = 0x0205d593; // srli a1, a1, 32
        buf[idx++] = 0x00b50533; // add a0, a0, a1
    }

    // Add lower 12 bits
    if (imm_11_0 != 0 || (imm_11_0 & 0x800)) {
        buf[idx++] = 0x00050513 | (imm_11_0 << 20); // addi a0, a0, imm_11_0
    }

    // Return
    // buf[idx++] = 0x00008067; // ret
}

unsigned long long mean(unsigned long long *arr, unsigned int len) {
    unsigned long long sum = 0;
    for (int i = 0; i < len; i++)
        sum += arr[i];
    return sum / len;
}

unsigned long long access_time(void*p) {
    unsigned long long start;
    MFENCE;
    start = rdtsc();
    maccess(p);
    MFENCE;
    return rdtsc() - start;
}

unsigned long long get_dram_timing(eviction_set_t* set) {

    for (unsigned long i = 0; i < NUM_SAMPLES; i++) {
#ifdef P550
        evict_with_set(set);
#elif defined(C910)
        flush(test_buf);
#else
#error Unsupported Hardware
#endif
        MFENCE;
        times[i] = access_time(test_buf);
    }

    return mean(times, NUM_SAMPLES);
}

int main() {
    unsigned long long start, t_acc, dest = (unsigned long long) ret_dest, dram_time;
    unsigned long i;
    unsigned char* codebuf;
    void (*dynfunct)(unsigned long long*);
    struct {
        const char* name;
        unsigned int insn;
    }
    fences[] = {
        {"nop", 0x00010001},
        {"infinite loop", 0xa001a001},
        {"fence rw,rw", f_rwrw},
        {"fence r,r", f_rr},
        {"fence w,w", f_ww},
        {"fence r,w", f_rw},
        {"fence w,r", f_wr},
        {"fence.i", f_i},
    };
    eviction_set_t evset = {0, }, evset_dest = {0, };

    // Pin to CPU 0
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
    }

#ifdef P550
    cacheutils_init();
#endif

    test_buf[0] = 1;
    sync();

#ifdef P550
    if (getuid() != 0) {
        printf("I need root privileges\n");
        exit(EXIT_FAILURE);
    }

    build_eviction_set_vaddr(test_buf, &evset);
    build_eviction_set_vaddr(&dest, &evset_dest);
#endif

    get_dram_timing(&evset);
    dram_time = get_dram_timing(&evset);

    printf("%18s: %llu\n", "DRAM access time", dram_time);

    test_function_code(&dest);

    // Allocate JIT code buffer
    codebuf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    dynfunct = (void (*)(unsigned long long*)) codebuf;
    // Copy code to JIT buffer
    memcpy(codebuf, test_function_code, (unsigned char*) test_function_code_end - (unsigned char*) test_function_code);
    // Add load from immediate instructions
    fill_load_immediate((void*)((unsigned char*) codebuf + ((unsigned char*) fence_dest - (unsigned char*) test_function_code) + 16), (uint64_t) test_buf);

    // Keep consistency
    asm volatile ("fence.i");
    dynfunct(&dest);

    // Add pointer to ret_dest in code buffer to dest
    dest = (unsigned long long) (codebuf + ((unsigned char*) ret_dest - (unsigned char*) test_function_code));

    // For each fence
    for (unsigned int f = 0; f < countof(fences); f++) {
        // JIT fence into code
        *(uint32_t*)&codebuf[(unsigned char*) fence_dest - (unsigned char*) test_function_code] = fences[f].insn;

        // Keep consistency
        asm volatile ("fence.i");
        sync();

        // Measure timings
        for (i = 0; i < NUM_SAMPLES; i++) {
#ifdef P550
            evict_with_set(&evset_dest);
            evict_with_set(&evset);
#elif defined(C910)
            flush(test_buf);
            flush(&dest);
#else
#error Unsupported Hardware
#endif
            MFENCE;
            dynfunct(&dest);
            times[i] = access_time(test_buf);
        }
        t_acc = mean(times, NUM_SAMPLES);
        printf("%18s: %3llu %s\n", fences[f].name, t_acc, t_acc < (dram_time - (dram_time / 5) ) ? "   <- Allows speculation" : "");
    }

    // Special case - retpoline is larger than the fence instructions
    memcpy(&codebuf[(unsigned char*) fence_dest - (unsigned char*) test_function_code], (void*) retpoline, (unsigned char*) retpoline_end - (unsigned char*) retpoline);
    asm volatile ("fence.i");

    for (i = 0; i < NUM_SAMPLES; i++) {
        evict_with_set(&evset_dest);
        evict_with_set(&evset);
        MFENCE;
        dynfunct(&dest);
        times[i] = access_time(test_buf);
    }
    t_acc = mean(times, NUM_SAMPLES);
    printf("%18s: %3llu %s\n", "retpoline", t_acc, t_acc < (dram_time - (dram_time / 5) ) ? "   <- Allows speculation" : "");

    return 0;
}
