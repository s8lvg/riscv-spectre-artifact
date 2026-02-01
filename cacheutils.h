/**
 * RISC-V Cache Utils - Unified primitives for Spectre research
 *
 * Usage:
 *   size_t CACHE_MISS = 0;
 *   #include "cacheutils.h"
 *
 *   cacheutils_init();
 *   CACHE_MISS = detect_flush_reload_threshold();
 *   flush(&probe_array[i * 4096]);
 *   cacheutils_cleanup();
 *
 * Platform flags: -DP550 (SiFive P550) or -DC910 (T-Head C910)
 */

#ifndef _CACHEUTILS_H_
#define _CACHEUTILS_H_

#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>

// ============================================================================
// Platform Configuration
// ============================================================================

#if defined(P550) || defined(LAB77)
    #define CACHEUTILS_USE_EVICTION
#endif

// ============================================================================
// Constants
// ============================================================================

#define PAGE_SIZE 4096
#define HUGE_PAGE_SIZE (2 * 1024 * 1024)
#define NUM_EVICTION_HUGEPAGES 32
#define EVICTION_CHAIN_SIZE 32

// ============================================================================
// CPU-Specific Macros
// ============================================================================

/* Cache Set Extraction */
#ifdef C910
    #define GET_CACHE_SET_PADDR(addr) (((addr) >> 6) & 0x3FF)   // L2 1024 sets (10 bits)
#elif defined(P550)
    #define GET_CACHE_SET_PADDR(addr) (((addr) >> 6) & 0x1FFF)  // L3 8192 sets (13 bits)
#else
    #error "Must define either C910 or P550 CPU type"
#endif

/* Prevent compiler optimization */
#define NO_OPT(value) do { \
    volatile __typeof__(value) _tmp = (value); \
    asm volatile("" : "+r"(_tmp) : : "memory"); \
    (void)_tmp; \
} while(0)

// ============================================================================
// Type Definitions
// ============================================================================

typedef struct {
    void* eviction_addrs[128];      // Eviction set addresses
    int num_addrs;                  // Number of addresses in set
    uintptr_t* chain_start;         // Pointer-chasing chain start (NULL if not built)
    int chain_length;               // Number of addresses in chain
} eviction_set_t;

// ============================================================================
// Global State
// ============================================================================

// Eviction buffers: hugepages for building eviction sets
// P550: 32 pages × 4 lines/set/page = ~128 addresses per set
// C910: 32 pages × 32 lines/set/page = ~1024 addresses per set
static char *g_cacheutils_eviction_buffers[NUM_EVICTION_HUGEPAGES] = {NULL};

// Perf event file descriptor for rdcycle on P550
static int g_cacheutils_perf_fd = -1;

// ============================================================================
// Forward Declarations
// ============================================================================

static int cacheutils_init_eviction_buffer(void);

// ============================================================================
// Initialization
// ============================================================================

#include <sys/syscall.h>
#include <sys/utsname.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <limits.h>

/**
 * Check if kernel version >= major.minor
 * @return 1 if at least specified version, 0 if older, -1 on error
 */
static int kernel_version_at_least(int major, int minor) {
    struct utsname buf;
    if (uname(&buf) != 0) {
        return -1;
    }

    int kmajor = 0, kminor = 0;
    if (sscanf(buf.release, "%d.%d", &kmajor, &kminor) < 2) {
        return -1;
    }

    if (kmajor > major) return 1;
    if (kmajor < major) return 0;
    return kminor >= minor;
}

/**
 * Initialize cacheutils library
 *
 * On kernels >= 6.6: Enables rdcycle via perf_event (disabled by default)
 * On older kernels: rdcycle works without perf workaround
 * Both: Allocates hugepage eviction buffers if needed
 *
 * @return 0 on success, -1 on failure
 */
static int cacheutils_init() {
    if (g_cacheutils_perf_fd >= 0) {
        return 0;
    }

    // The rdcycle CSR was disabled by default starting in kernel 6.6
    // (commit cc4c07c89aada). Only use perf workaround on newer kernels.
    int needs_perf_workaround = kernel_version_at_least(6, 6);

    if (needs_perf_workaround < 0) {
        fprintf(stderr, "ERROR: Failed to parse kernel version\n");
        return -1;
    }

    if (needs_perf_workaround) {
        struct perf_event_attr pe;
        memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.type = PERF_TYPE_HARDWARE;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = PERF_COUNT_HW_CPU_CYCLES;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;

        g_cacheutils_perf_fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
        if (g_cacheutils_perf_fd < 0) {
            perror("ERROR: perf_event_open failed (rdcycle may not work)");
            return -1;
        }

        if (ioctl(g_cacheutils_perf_fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
            perror("ERROR: PERF_EVENT_IOC_ENABLE failed");
            close(g_cacheutils_perf_fd);
            g_cacheutils_perf_fd = -1;
            return -1;
        }
    } else {
        // Old kernel: rdcycle works without perf, use sentinel value
        g_cacheutils_perf_fd = INT_MAX;
    }

#ifdef CACHEUTILS_USE_EVICTION
    // Initialize eviction buffer (only needed for P550 without flush instruction)
    if (cacheutils_init_eviction_buffer() != 0) {
        if (g_cacheutils_perf_fd >= 0) {
            close(g_cacheutils_perf_fd);
            g_cacheutils_perf_fd = -1;
        }
        return -1;
    }
#endif

    return 0;
}

// Cleanup: close perf_event fd and free eviction buffers
static void cacheutils_cleanup() {
    if (g_cacheutils_perf_fd >= 0 && g_cacheutils_perf_fd != INT_MAX) {
        close(g_cacheutils_perf_fd);
    }
    g_cacheutils_perf_fd = -1;
#ifdef CACHEUTILS_USE_EVICTION
    // Clean up all eviction hugepages (only needed for P550)
    for (int i = 0; i < NUM_EVICTION_HUGEPAGES; i++) {
        if (g_cacheutils_eviction_buffers[i] != NULL) {
            munmap(g_cacheutils_eviction_buffers[i], HUGE_PAGE_SIZE);
            g_cacheutils_eviction_buffers[i] = NULL;
        }
    }
#endif
}

// ============================================================================
// Timing Primitives
// ============================================================================

// Read cycle counter with memory fences (serializing)
__attribute__((always_inline))
static inline uint64_t rdtsc() {
    uint64_t val;
    asm volatile("fence rw,rw" : : : "memory");
    asm volatile ("rdcycle %0" : "=r"(val));
    asm volatile("fence rw,rw" : : : "memory");
    return val;
}

// ============================================================================
// Cache Operations
// ============================================================================

// D-cache flush using DCACHE.CIVA instruction (C910 only)
// https://wiki.attacking.systems/en/compiler/unknown-instructions
__attribute__((always_inline))
static inline void flush(void* addr) {
    asm volatile("xor a7, a7, a7\n"
                 "add a7, a7, %0\n"
                 ".long 0x278800b" // DCACHE.CIVA a7
                 : : "r"(addr) : "a7","memory");
}

// Memory access (loads 8 bytes)
__attribute__((always_inline))
static inline void maccess(void* addr) {
    asm volatile("ld a7, (%0)" : : "r"(addr) : "a7","memory");
}

// Memory fence (all loads/stores complete before continuing)
__attribute__((always_inline))
static inline void mfence() {
    asm volatile("fence rw,rw" : : : "memory");
}

// Anti-speculation fence (all loads complete before continuing)
__attribute__((always_inline))
static inline void nospec() {
    asm volatile("fence r,rw" : : : "memory");
}

// ============================================================================
// Speculation Control
// ============================================================================

// STALL - Extend speculation window via slow floating-point division chain
// Uses 4 fdiv.s instructions to delay value resolution while maintaining correctness
// Returns the input value unchanged but delayed through FP division pipeline
#define STALL(value) ({ \
    volatile int _v = (int)(value); \
    volatile int _result; \
    asm volatile( \
        "fcvt.s.w  fa5, %1\n\t"      /* Convert input to float */ \
        "fmv.w.x   fa4, zero\n\t"    /* Load 1.0 */ \
        "lui       t0, 0x3f800\n\t"  /* 0x3f800000 = 1.0 in IEEE 754 */ \
        "fmv.w.x   fa4, t0\n\t" \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fdiv.s    fa5, fa5, fa4\n\t" /* Divide by 1.0 (stall) */ \
        "fcvt.w.s  %0, fa5, rtz\n\t"  /* Convert back to int */ \
        : "=r"(_result) \
        : "r"(_v) \
        : "fa4", "fa5", "t0", "memory"); \
    _result; \
})

/* Helper macros for stringifying preprocessor values */
#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

/* PTR_CHASE_N: Chase through pointer chain with configurable depth
 * @param start_ptr: Starting pointer of the chain
 * @param depth: Number of pointer dereferences (must be compile-time constant)
 * Returns: final pointer value after N loads */
#define PTR_CHASE_N(start_ptr, depth) ({ \
    void* _p = (void*)(start_ptr); \
    void* _result; \
    asm volatile( \
        ".rept " STRINGIFY(depth) "\n\t" \
        "ld %0, 0(%0)\n\t" \
        ".endr\n\t" \
        : "=r"(_result) \
        : "0"(_p) \
        : "memory"); \
    _result; \
})

// ============================================================================
// Utility Functions
// ============================================================================

// Pin thread to CPU core
static inline int pin_to_core(int core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
        return -1;
    }
    return 0;
}

// ============================================================================
// Physical Address Translation
// ============================================================================

// Extract physical frame number from pagemap entry
static inline uint64_t get_frame_number_from_pme(uint64_t pme) {
    return pme & ((1ULL << 54) - 1);
}

// Translate virtual to physical address via /proc/self/pagemap
// @return Physical address, or 0 on failure (needs root)
static inline size_t get_physical_address(size_t vaddr) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd == -1) {
        perror("get_physical_address: Could not open /proc/self/pagemap");
        return 0;
    }

    uint64_t virtual_addr = (uint64_t)vaddr;
    size_t pme = 0;
    off_t offset = (virtual_addr / 4096) * sizeof(pme);
    int got = pread(fd, &pme, sizeof(pme), offset);
    uint64_t pfn = get_frame_number_from_pme(pme);

    if (got != sizeof(pme) || pfn == 0) {
        fprintf(stderr, "ERROR: Cannot translate address 0x%lx (need root for /proc/self/pagemap)\n", virtual_addr);
        close(fd);
        return 0;
    }

    // Check if page is present
    if (!(pme & (1ULL << 63))) {
        fprintf(stderr, "ERROR: Page not present at 0x%lx\n", virtual_addr);
        close(fd);
        return 0;
    }

    close(fd);
    return (pfn << 12) | ((size_t)vaddr & 0xFFFULL);
}

// ============================================================================
// Eviction Set Building
// ============================================================================

static int cacheutils_init_eviction_buffer() {
    // Check if already initialized
    if (g_cacheutils_eviction_buffers[0] != NULL) {
        return 0;
    }

    // Allocate multiple 2MB hugepages for eviction sets
    for (int i = 0; i < NUM_EVICTION_HUGEPAGES; i++) {
        g_cacheutils_eviction_buffers[i] = mmap(NULL, HUGE_PAGE_SIZE,
                                                PROT_READ | PROT_WRITE,
                                                MAP_ANON|MAP_PRIVATE|MAP_HUGETLB,
                                                0, 0);

        if (g_cacheutils_eviction_buffers[i] == MAP_FAILED) {
            // Clean up previously allocated hugepages
            for (int j = 0; j < i; j++) {
                munmap(g_cacheutils_eviction_buffers[j], HUGE_PAGE_SIZE);
                g_cacheutils_eviction_buffers[j] = NULL;
            }
            fprintf(stderr, "ERROR: Failed to allocate hugepage %d/%d (need %d x 2MB)\n",
                    i + 1, NUM_EVICTION_HUGEPAGES, NUM_EVICTION_HUGEPAGES);
            fprintf(stderr, "Run: sudo sysctl -w vm.nr_hugepages=%d\n", NUM_EVICTION_HUGEPAGES + 10);
            return -1;
        }

        // Touch all pages to ensure they're mapped
        memset(g_cacheutils_eviction_buffers[i], 0xFF, HUGE_PAGE_SIZE);
        for (size_t j = 0; j < HUGE_PAGE_SIZE; j += PAGE_SIZE) {
            g_cacheutils_eviction_buffers[i][j] = 0xAA;
        }
    }

    return 0;
}

/**
 * Build pointer-chasing chain for eviction traversal
 *
 * Creates sequential chain: addr[0]->addr[1]->...->addr[N-1]->NULL
 *
 * @param set Eviction set with addresses to chain
 * @param chain_size Number of addresses to use in chain
 * @return 0 on success, -1 on failure (insufficient addresses)
 */
static inline int build_eviction_chain(eviction_set_t* set, int chain_size) {
    if (set->num_addrs < chain_size) {
        set->chain_start = NULL;
        set->chain_length = 0;
        return -1;  // Not enough addresses for requested chain size
    }

    // Build simple sequential chain: addr[0] -> addr[1] -> ... -> addr[N-1] -> NULL
    for (int i = 0; i < chain_size - 1; i++) {
        uintptr_t* current = (uintptr_t*)set->eviction_addrs[i];
        uintptr_t* next = (uintptr_t*)set->eviction_addrs[i + 1];
        *current = (uintptr_t)next;
    }

    // NULL-terminate the chain
    uintptr_t* last = (uintptr_t*)set->eviction_addrs[chain_size - 1];
    *last = 0;

    // Store chain info
    set->chain_start = (uintptr_t*)set->eviction_addrs[0];
    set->chain_length = chain_size;

    return 0;
}

/**
 * Build eviction set from physical address
 *
 * Scans hugepages for addresses mapping to same cache set
 * Cache configs: C910 L2 1024 sets (bits [6:15]), P550 L3 8192 sets (bits [6:18])
 *
 * @param phys_addr Target physical address
 * @param set Output eviction set (with pointer-chasing chain)
 * @return 0 on success (≥32 addrs), -1 on failure
 */
static inline int build_eviction_set_paddr(uintptr_t phys_addr, eviction_set_t* set) {
    if (g_cacheutils_eviction_buffers[0] == NULL) {
        set->num_addrs = 0;
        return -1;
    }

    // Extract cache set index from physical address
    uintptr_t target_set = GET_CACHE_SET_PADDR(phys_addr);

    set->num_addrs = 0;
    set->chain_start = NULL;  // Chain not built yet
    set->chain_length = 0;

    // Scan through all eviction hugepages at cache line granularity (64 bytes)
    // For hugepages: virt_addr[20:0] == phys_addr[20:0], so we can use virt bits directly
    for (int buf_idx = 0; buf_idx < NUM_EVICTION_HUGEPAGES && set->num_addrs < 128; buf_idx++) {
        if (g_cacheutils_eviction_buffers[buf_idx] == NULL) continue;

        char* current = g_cacheutils_eviction_buffers[buf_idx];
        char* end = current + HUGE_PAGE_SIZE;

        while (set->num_addrs < 128 && current < end) {
            // For hugepages, lower 21 bits of vaddr == paddr
            // Cache set from virtual address (same bits as physical)
            uintptr_t candidate_set = GET_CACHE_SET_PADDR((uintptr_t)current);

            // Only add if cache set matches
            if (candidate_set == target_set) {
                set->eviction_addrs[set->num_addrs++] = current;
            }

            current += 64;  // Next cache line
        }
    }

    if (set->num_addrs < EVICTION_CHAIN_SIZE) {
        return -1;  // Need enough addresses for effective eviction
    }

    // Build pointer-chasing chain
    build_eviction_chain(set, EVICTION_CHAIN_SIZE);

    return 0;
}

/**
 * Build eviction set from virtual address
 *
 * Translates virtual to physical address, then builds eviction set
 * Use for non-hugepage variables (e.g., stack/global variables)
 *
 * @param vaddr Target virtual address
 * @param set Output eviction set
 * @return 0 on success, -1 on failure (translation or insufficient addrs)
 */
static inline int build_eviction_set_vaddr(void* vaddr, eviction_set_t* set) {
    // Translate virtual address to physical address
    size_t phys_addr = get_physical_address((size_t)vaddr);
    if (phys_addr == 0) {
        set->num_addrs = 0;
        set->chain_start = NULL;
        set->chain_length = 0;
        return -1;
    }

    // Use the physical address to build the eviction set
    return build_eviction_set_paddr(phys_addr, set);
}

// ============================================================================
// Eviction Implementation
// ============================================================================

/* Uses pointer-chasing traversal with EVICTION_CHAIN_SIZE cache-congruent addresses.
 * - Pre-builds linked list: addr[0] -> addr[1] -> ... -> addr[N-1] -> NULL
 * - Traversal: ptr = (uintptr_t*)*ptr in a tight loop
 * - Benefits: No index calculations, tight loop, good cache behavior
 * - Verified on P550 with libeviction: 99.9% TPR, 32 addresses sufficient
 */

// Evict cache using pointer-chasing (inline asm prevents compiler reordering)
__attribute__((always_inline))
static inline void evict_with_set(eviction_set_t* set) {
    // Pointer chasing in assembly to prevent compiler reordering
    register uintptr_t *ptr = set->chain_start;

    asm volatile(
        "1:\n\t"
        "beqz %0, 2f\n\t"        // if ptr == NULL, exit
        "ld %0, 0(%0)\n\t"       // ptr = *ptr
        "j 1b\n\t"               // loop
        "2:\n\t"
        : "+r"(ptr)              // ptr is read and written
        :
        : "memory"
    );

    // Memory fence
    mfence();
}

// Evict+Reload with timing using precomputed eviction set
// @param ptr Address to measure, @param set Eviction set for ptr's cache line
__attribute__((always_inline))
static inline uint64_t evict_reload_t(void* ptr, eviction_set_t* set) {
    uint64_t start = rdtsc();
    maccess(ptr);
    uint64_t end = rdtsc();
    evict_with_set(set);
    mfence();
    return end - start;
}

// PRIME+PROBE: Traverse eviction set with timing
// Evicts cache set and measures reload time (for PRIME+PROBE attacks)
// @param set Eviction set to traverse
// @return Access time in cycles (higher = cache was evicted by victim)
__attribute__((always_inline))
static inline uint64_t prime_t(eviction_set_t* set) {
    uint64_t start = rdtsc();
    evict_with_set(set);
    uint64_t end = rdtsc();
    return end - start;
}

// ============================================================================
// Multi-Chain Pointer Chase API
// ============================================================================

/**
 * Pointer chase chain structure
 * Supports multiple independent chains with configurable depth
 */
typedef struct {
    void** pages;       // Array of page pointers [depth]
    void* start;        // Chain start (pages[0])
    int depth;          // Number of chain nodes
#ifdef CACHEUTILS_USE_EVICTION
    eviction_set_t* evsets;  // Eviction sets for each page [depth]
#endif
} ptr_chase_chain_t;

/**
 * Create a pointer chase chain
 *
 * Allocates 'depth' pages linked together, ending at final_target.
 * Chain: pages[0] -> pages[1] -> ... -> pages[depth-1] -> final_target
 *
 * @param depth Number of pointer dereferences (must match PTR_CHASE_N depth)
 * @param final_target Pointer the chain resolves to after 'depth' loads
 * @return Allocated chain, or NULL on failure
 */
static ptr_chase_chain_t* ptr_chase_chain_create(int depth, void* final_target) {
    ptr_chase_chain_t* chain = (ptr_chase_chain_t*)malloc(sizeof(ptr_chase_chain_t));
    if (!chain) return NULL;

    chain->depth = depth;
    chain->pages = (void**)malloc(depth * sizeof(void*));
    if (!chain->pages) {
        free(chain);
        return NULL;
    }

#ifdef CACHEUTILS_USE_EVICTION
    chain->evsets = (eviction_set_t*)malloc(depth * sizeof(eviction_set_t));
    if (!chain->evsets) {
        free(chain->pages);
        free(chain);
        return NULL;
    }
#endif

    // Allocate pages
    for (int i = 0; i < depth; i++) {
        chain->pages[i] = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (chain->pages[i] == MAP_FAILED) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                munmap(chain->pages[j], PAGE_SIZE);
            }
#ifdef CACHEUTILS_USE_EVICTION
            free(chain->evsets);
#endif
            free(chain->pages);
            free(chain);
            return NULL;
        }
        *(volatile char*)chain->pages[i] = 0;  // Touch to map
    }

    // Build pointer chain
    for (int i = 0; i < depth - 1; i++) {
        *(void**)chain->pages[i] = chain->pages[i + 1];
    }
    *(void**)chain->pages[depth - 1] = final_target;
    chain->start = chain->pages[0];

#ifdef CACHEUTILS_USE_EVICTION
    // Build eviction sets for each page
    for (int i = 0; i < depth; i++) {
        if (build_eviction_set_vaddr(chain->pages[i], &chain->evsets[i]) != 0) {
            // Cleanup on failure
            for (int j = 0; j < depth; j++) {
                munmap(chain->pages[j], PAGE_SIZE);
            }
            free(chain->evsets);
            free(chain->pages);
            free(chain);
            return NULL;
        }
    }
#endif

    return chain;
}

/**
 * Evict all chain pages from cache
 *
 * Call before PTR_CHASE_N to ensure cache misses create latency.
 *
 * @param chain Chain to evict
 */
static inline void ptr_chase_chain_evict(ptr_chase_chain_t* chain) {
#ifdef CACHEUTILS_USE_EVICTION
    for (int i = 0; i < chain->depth; i++) {
        evict_with_set(&chain->evsets[i]);
    }
#else
    for (int i = 0; i < chain->depth; i++) {
        flush(chain->pages[i]);
    }
#endif
}

/**
 * Destroy a pointer chase chain
 *
 * Frees all allocated resources.
 *
 * @param chain Chain to destroy
 */
static void ptr_chase_chain_destroy(ptr_chase_chain_t* chain) {
    if (!chain) return;

    for (int i = 0; i < chain->depth; i++) {
        if (chain->pages[i]) {
            munmap(chain->pages[i], PAGE_SIZE);
        }
    }
#ifdef CACHEUTILS_USE_EVICTION
    free(chain->evsets);
#endif
    free(chain->pages);
    free(chain);
}

// ============================================================================
// Cache Timing Primitives
// ============================================================================

// Flush+Reload with timing: Returns the access time in cycles
// Note: On P550, use evict_reload_t() with eviction sets instead
__attribute__((always_inline))
static inline size_t flush_reload_t(void *ptr) {
  uint64_t start = rdtsc();
  maccess(ptr);
  uint64_t end = rdtsc();
  flush(ptr);
  mfence();
  return end - start;
}

// Reload with timing: Returns the access time in cycles
__attribute__((always_inline))
static inline size_t reload_t(void *ptr) {
  uint64_t start = rdtsc();
  maccess(ptr);
  uint64_t end = rdtsc();
  return end - start;
}

// ============================================================================
// Threshold Detection
// ============================================================================

// Helper functions for statistics

static int cacheutils_comp(const void *elem1, const void *elem2) {
  uint64_t f = *((uint64_t *)elem1);
  uint64_t s = *((uint64_t *)elem2);
  if (f > s) return 1;
  if (f < s) return -1;
  return 0;
}

static uint64_t findMedianSorted(uint64_t *v, int offset, int len) {
  if (len % 2 == 0) {
    return (v[(len + offset) / 2 - 1] + v[(len + offset) / 2]) / 2;
  } else {
    return v[(len + offset) / 2];
  }
}

/**
 * Filter outliers from sorted array using IQR (Interquartile Range) method
 *
 * Removes values outside Q1 - 1.5*IQR to Q3 + 1.5*IQR range
 * Array must be sorted before calling this function
 *
 * @param measures Sorted array of measurements (modified in-place)
 * @param len Length of array
 * @return New length after filtering, or -1 on error
 */
static int filter_outliers_iqr(uint64_t *measures, int len) {
  if (len < 4) {
    return len;  // Need at least 4 samples for quartiles
  }

  // Calculate quartiles
  uint64_t Q1 = findMedianSorted(measures, 0, len / 2);
  uint64_t Q3 = findMedianSorted(measures, len / 2, len);
  uint64_t IQR = Q3 - Q1;

  // Filter samples outside IQR bounds (1.5x multiplier)
  int count = 0;
  for (int i = 0; i < len; i++) {
    if (measures[i] >= Q1 - (IQR * 3 / 2) && measures[i] <= Q3 + (IQR * 3 / 2)) {
      measures[count] = measures[i];
      count++;
    }
  }

  return count;
}

/**
 * Auto-detect Flush+Reload threshold with IQR outlier filtering
 *
 * Collects 5000 cache hit and miss samples, filters outliers, and computes
 * threshold as weighted average of median timings (closer to hit time)
 *
 * @return Threshold in cycles (weighted 2:1 toward hit timing), or 15 on error
 */
static size_t detect_flush_reload_threshold() {
  size_t count = 5000;
  size_t dummy[16];
  size_t *ptr = dummy + 8;

  // Allocate separate arrays for cache hits and misses
  uint64_t *cache_hits = malloc(count * sizeof(uint64_t));
  uint64_t *cache_misses = malloc(count * sizeof(uint64_t));

  if (cache_hits == NULL || cache_misses == NULL) {
    fprintf(stderr, "Warning: Could not allocate memory for threshold detection\n");
    free(cache_hits);
    free(cache_misses);
    return 15;  // Fallback to safe default for P550
  }

  maccess(ptr);

  // Collect cache hit measurements (reload with data in cache)
  for (size_t i = 0; i < count; i++) {
    cache_hits[i] = reload_t(ptr);
  }

  // Collect cache miss measurements (flush+reload)
#ifdef CACHEUTILS_USE_EVICTION
  eviction_set_t set;
  if (build_eviction_set_vaddr(ptr, &set) != 0) {
    fprintf(stderr, "Warning: Could not build eviction set for threshold detection\n");
    free(cache_hits);
    free(cache_misses);
    // TODO: Error out if we dont find eviction set
    return 15;  // Fallback to safe default for P550
  }
  for (size_t i = 0; i < count; i++) {
    cache_misses[i] = evict_reload_t(ptr, &set);
  }
#else
  for (size_t i = 0; i < count; i++) {
    cache_misses[i] = flush_reload_t(ptr);
  }
#endif

  // Sort arrays for IQR outlier filtering
  qsort(cache_hits, count, sizeof(uint64_t), cacheutils_comp);
  qsort(cache_misses, count, sizeof(uint64_t), cacheutils_comp);

  // Filter outliers from each distribution separately
  int filtered_hit_count = filter_outliers_iqr(cache_hits, count);
  int filtered_miss_count = filter_outliers_iqr(cache_misses, count);

  if (filtered_hit_count <= 0 || filtered_miss_count <= 0) {
    free(cache_hits);
    free(cache_misses);
    // TODO: Error out here if we filter to much
    return 15;  // Fallback if filtering removed everything
  }

  // Use median instead of mean to avoid overflow and be more robust to outliers
  // Arrays are already sorted from IQR filtering
  size_t avg_hit_time = cache_hits[filtered_hit_count / 2];
  size_t avg_miss_time = cache_misses[filtered_miss_count / 2];

  free(cache_hits);
  free(cache_misses);

  // Threshold = weighted average (closer to hit time to avoid false negatives)
  return (avg_miss_time + avg_hit_time * 2) / 3;
}

#endif /* _CACHEUTILS_H_ */
