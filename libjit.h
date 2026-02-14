/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 Lukas Gerlach
 *
 * RISC-V JIT Library - Runtime code generation for Spectre research
 *
 * Kernel compatible: Works in both userspace and kernel modules.
 * In kernel context, uses vmalloc + set_memory_x for executable memory.
 *
 * Usage:
 *   #include "libjit.h"
 *
 *   jit_buf_t* buf = jit_init(PAGE_SIZE * 10);
 *   jit_li(buf, A0, 0x1234);
 *   jit_ret(buf);
 *   void (*fn)(void) = jit_finalize(buf);
 *   fn();
 *   jit_free(buf);
 */

#ifndef _LIBJIT_H_
#define _LIBJIT_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>
#else
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#endif

// ============================================================================
// Type Definitions
// ============================================================================

/* JIT code buffer */
typedef struct {
    uint32_t* code;      /* Pointer to executable buffer */
    size_t capacity;     /* Total capacity in instructions (uint32_t) */
    size_t idx;          /* Current write position */
} jit_buf_t;

/* RISC-V register encoding */
typedef enum {
    ZERO = 0, RA = 1, SP = 2, GP = 3, TP = 4,
    T0 = 5, T1 = 6, T2 = 7,
    S0 = 8, FP = 8, S1 = 9,
    A0 = 10, A1 = 11, A2 = 12, A3 = 13, A4 = 14, A5 = 15, A6 = 16, A7 = 17,
    S2 = 18, S3 = 19, S4 = 20, S5 = 21, S6 = 22, S7 = 23, S8 = 24, S9 = 25, S10 = 26, S11 = 27,
    T3 = 28, T4 = 29, T5 = 30, T6 = 31
} reg_t;

// ============================================================================
// Buffer Management Functions
// ============================================================================

/* Initialize JIT buffer */
static inline jit_buf_t* jit_init(size_t capacity) {
#ifdef __KERNEL__
    jit_buf_t* buf = kmalloc(sizeof(jit_buf_t), GFP_KERNEL);
    if (!buf) return NULL;

    size_t bytes = capacity * sizeof(uint32_t);
    buf->code = __vmalloc(bytes, GFP_KERNEL);
    if (!buf->code) {
        kfree(buf);
        return NULL;
    }
    set_memory_x((unsigned long)buf->code, (bytes + PAGE_SIZE - 1) / PAGE_SIZE);
#else
    jit_buf_t* buf = malloc(sizeof(jit_buf_t));
    if (!buf) return NULL;

    size_t bytes = capacity * sizeof(uint32_t);
    buf->code = mmap(NULL, bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf->code == MAP_FAILED) {
        free(buf);
        return NULL;
    }
#endif

    buf->capacity = capacity;
    buf->idx = 0;
    return buf;
}

/* Free JIT buffer */
static inline void jit_free(jit_buf_t* buf) {
    if (!buf) return;
#ifdef __KERNEL__
    if (buf->code) {
        vfree(buf->code);
    }
    kfree(buf);
#else
    if (buf->code && buf->code != MAP_FAILED) {
        munmap(buf->code, buf->capacity * sizeof(uint32_t));
    }
    free(buf);
#endif
}

/* Get function pointer to start of code */
static inline void* jit_finalize(jit_buf_t* buf) {
    asm volatile("fence.i" ::: "memory");
    return (void*)buf->code;
}

/* Get current PC (address of next instruction) */
static inline void* jit_get_pc(jit_buf_t* buf) {
    return (void*)&buf->code[buf->idx];
}

/* Emit raw 32-bit instruction */
static inline void jit_emit(jit_buf_t* buf, uint32_t insn) {
    if (buf->idx >= buf->capacity) {
#ifndef __KERNEL__
        fprintf(stderr, "JIT buffer overflow\n");
#endif
        return;
    }
    buf->code[buf->idx++] = insn;
}

// ============================================================================
// RISC-V Instruction Encoders
// ============================================================================

/* LUI: Load Upper Immediate - lui rd, imm */
static inline void jit_lui(jit_buf_t* buf, reg_t rd, uint32_t imm) {
    uint32_t insn = 0x37 | (rd << 7) | ((imm & 0xfffff) << 12);
    jit_emit(buf, insn);
}

/* ADDI: Add Immediate - addi rd, rs1, imm */
static inline void jit_addi(jit_buf_t* buf, reg_t rd, reg_t rs1, int16_t imm) {
    uint32_t insn = 0x13 | (rd << 7) | (0x0 << 12) | (rs1 << 15) | ((imm & 0xfff) << 20);
    jit_emit(buf, insn);
}

/* JAL: Jump and Link - jal rd, offset */
static inline void jit_jal(jit_buf_t* buf, reg_t rd, int32_t offset) {
    /* Offset is in bytes, must be even (2-byte aligned) */
    /* Encoded as: imm[20|10:1|11|19:12] | rd | 1101111 */
    uint32_t imm_20 = (offset >> 20) & 0x1;
    uint32_t imm_19_12 = (offset >> 12) & 0xff;
    uint32_t imm_11 = (offset >> 11) & 0x1;
    uint32_t imm_10_1 = (offset >> 1) & 0x3ff;

    uint32_t insn = 0x6f | (rd << 7) |
                    (imm_19_12 << 12) | (imm_11 << 20) |
                    (imm_10_1 << 21) | (imm_20 << 31);
    jit_emit(buf, insn);
}

/* JALR: Jump and Link Register - jalr rd, rs1, imm */
static inline void jit_jalr(jit_buf_t* buf, reg_t rd, reg_t rs1, int16_t imm) {
    uint32_t insn = 0x67 | (rd << 7) | (0x0 << 12) | (rs1 << 15) | ((imm & 0xfff) << 20);
    jit_emit(buf, insn);
}

/* RET: Return - alias for jalr x0, ra, 0 */
static inline void jit_ret(jit_buf_t* buf) {
    jit_jalr(buf, ZERO, RA, 0);
}

/* EBREAK: Environment break - trap for debugging */
static inline void jit_ebreak(jit_buf_t* buf) {
    jit_emit(buf, 0x00100073);  // ebreak
}

/* LD: Load Doubleword (RV64) - ld rd, imm(rs1) */
static inline void jit_ld(jit_buf_t* buf, reg_t rd, reg_t rs1, int16_t imm) {
    uint32_t insn = 0x03 | (rd << 7) | (0x3 << 12) | (rs1 << 15) | ((imm & 0xfff) << 20);
    jit_emit(buf, insn);
}

/* LB: Load Byte - lb rd, imm(rs1) */
static inline void jit_lb(jit_buf_t* buf, reg_t rd, reg_t rs1, int16_t imm) {
    uint32_t insn = 0x03 | (rd << 7) | (0x0 << 12) | (rs1 << 15) | ((imm & 0xfff) << 20);
    jit_emit(buf, insn);
}

/* LBU: Load Byte Unsigned - lbu rd, imm(rs1) */
static inline void jit_lbu(jit_buf_t* buf, reg_t rd, reg_t rs1, int16_t imm) {
    uint32_t insn = 0x03 | (rd << 7) | (0x4 << 12) | (rs1 << 15) | ((imm & 0xfff) << 20);
    jit_emit(buf, insn);
}

/* SD: Store Doubleword (RV64) - sd rs2, imm(rs1) */
static inline void jit_sd(jit_buf_t* buf, reg_t rs2, reg_t rs1, int16_t imm) {
    uint32_t imm_low = imm & 0x1f;
    uint32_t imm_high = (imm >> 5) & 0x7f;
    uint32_t insn = 0x23 | (imm_low << 7) | (0x3 << 12) | (rs1 << 15) | (rs2 << 20) | (imm_high << 25);
    jit_emit(buf, insn);
}

/* SLLI: Shift Left Logical Immediate - slli rd, rs1, shamt */
static inline void jit_slli(jit_buf_t* buf, reg_t rd, reg_t rs1, uint8_t shamt) {
    uint32_t insn = 0x00001013 | (rd << 7) | (rs1 << 15) | ((shamt & 0x3f) << 20);
    jit_emit(buf, insn);
}

/* ADD: Add two registers - add rd, rs1, rs2 */
static inline void jit_add(jit_buf_t* buf, reg_t rd, reg_t rs1, reg_t rs2) {
    uint32_t insn = 0x00000033 | (rd << 7) | (rs1 << 15) | (rs2 << 20);
    jit_emit(buf, insn);
}

// ============================================================================
// Branch Instructions
// ============================================================================

/* BGEU: Branch if greater or equal unsigned - bgeu rs1, rs2, offset */
static inline void jit_bgeu(jit_buf_t* buf, reg_t rs1, reg_t rs2, int16_t offset) {
    uint32_t imm_12 = (offset >> 12) & 0x1;
    uint32_t imm_11 = (offset >> 11) & 0x1;
    uint32_t imm_10_5 = (offset >> 5) & 0x3f;
    uint32_t imm_4_1 = (offset >> 1) & 0xf;

    uint32_t insn = 0x63 | (0x7 << 12) | (rs1 << 15) | (rs2 << 20) |
                    (imm_4_1 << 8) | (imm_11 << 7) | (imm_10_5 << 25) | (imm_12 << 31);
    jit_emit(buf, insn);
}

// ============================================================================
// Fence / Barrier Instructions
// ============================================================================

/* FENCE: Memory fence - fence iorw, iorw (full fence) */
static inline void jit_fence(jit_buf_t* buf) {
    jit_emit(buf, 0x0ff0000f);  // fence iorw, iorw
}

/* FENCE variants for testing speculation barriers */
static inline void jit_fence_rw_rw(jit_buf_t* buf) {
    jit_emit(buf, 0x0330000f);  // fence rw, rw (memory only, no I/O)
}

static inline void jit_fence_r_r(jit_buf_t* buf) {
    jit_emit(buf, 0x0220000f);  // fence r, r (read-read)
}

static inline void jit_fence_w_w(jit_buf_t* buf) {
    jit_emit(buf, 0x0110000f);  // fence w, w (write-write)
}

static inline void jit_fence_tso(jit_buf_t* buf) {
    jit_emit(buf, 0x8330000f);  // fence.tso (TSO ordering)
}

static inline void jit_fence_i(jit_buf_t* buf) {
    jit_emit(buf, 0x0000100f);  // fence.i (instruction fence)
}

/* PAUSE: Zihintpause - hint to stall pipeline */
static inline void jit_pause(jit_buf_t* buf) {
    jit_emit(buf, 0x0100000f);  // pause (fence w, 0 encoding)
}

/* CSR reads - might serialize on some implementations */
static inline void jit_rdcycle(jit_buf_t* buf, reg_t rd) {
    jit_emit(buf, 0xc0002073 | (rd << 7));  // rdcycle rd (csrr rd, cycle)
}

static inline void jit_rdtime(jit_buf_t* buf, reg_t rd) {
    jit_emit(buf, 0xc0102073 | (rd << 7));  // rdtime rd (csrr rd, time)
}

static inline void jit_rdinstret(jit_buf_t* buf, reg_t rd) {
    jit_emit(buf, 0xc0202073 | (rd << 7));  // rdinstret rd (csrr rd, instret)
}

/* LR.D: Load-Reserved Doubleword - lr.d rd, (rs1) */
static inline void jit_lr_d(jit_buf_t* buf, reg_t rd, reg_t rs1) {
    jit_emit(buf, 0x1000302f | (rd << 7) | (rs1 << 15));  // lr.d rd, (rs1)
}

// ============================================================================
// Higher-Level Helpers
// ============================================================================

/* Load immediate into register (handles full 64-bit values) */
static inline void jit_li(jit_buf_t* buf, reg_t rd, int64_t imm) {
    /* Check if it fits in 32 bits (sign-extended) */
    int32_t imm32 = (int32_t)imm;
    if ((int64_t)imm32 == imm) {
        /* Fits in 32 bits, use LUI + ADDI */
        int64_t upper = (imm + 0x800) >> 12;
        int64_t lower = imm & 0xfff;

        if (upper == 0 && lower < 2048) {
            /* Fits in 12 bits, just use addi */
            jit_addi(buf, rd, ZERO, lower);
        } else if (upper == -1 && lower >= 2048) {
            /* Negative number that fits in 12 bits after sign extension */
            jit_addi(buf, rd, ZERO, lower | 0xfffff000);
        } else {
            /* Need LUI + ADDI */
            jit_lui(buf, rd, upper & 0xfffff);
            if (lower != 0) {
                jit_addi(buf, rd, rd, lower);
            }
        }
    } else {
        /* Full 64-bit value */
        /* When we load the lower 32 bits with LUI+ADDI, it gets sign-extended to 64 bits. */
        /* So we need to compensate in the upper 32 bits. */

        /* Load lower 32 bits */
        int32_t lo32 = (int32_t)(imm & 0xffffffffULL);
        int64_t lo_upper = ((int64_t)lo32 + 0x800) >> 12;
        int64_t lo_lower = (int64_t)lo32 & 0xfff;

        if (lo_upper != 0) {
            jit_lui(buf, rd, lo_upper & 0xfffff);
            if (lo_lower != 0) {
                jit_addi(buf, rd, rd, lo_lower);
            }
        } else {
            jit_addi(buf, rd, ZERO, lo_lower);
        }
        /* Now rd contains the lower 32 bits, sign-extended to 64 bits */

        /* Calculate what the upper 32 bits actually are after sign extension */
        int64_t actual_val = (int64_t)lo32;  /* This is sign-extended */
        int64_t desired_upper = imm >> 32;
        int64_t actual_upper = actual_val >> 32;

        /* Only need to fix upper bits if they differ */
        if (desired_upper != actual_upper) {
            int64_t correction = desired_upper - actual_upper;

            /* Load correction into temp register */
            reg_t temp = T6;
            int64_t corr_upper = (correction + 0x800) >> 12;
            int64_t corr_lower = correction & 0xfff;

            if (corr_upper != 0) {
                jit_lui(buf, temp, corr_upper & 0xfffff);
                if (corr_lower != 0) {
                    jit_addi(buf, temp, temp, corr_lower);
                }
            } else {
                jit_addi(buf, temp, ZERO, corr_lower);
            }

            /* Shift correction left 32 bits */
            jit_slli(buf, temp, temp, 32);

            /* Add correction to rd */
            jit_add(buf, rd, rd, temp);
        }
    }
}

// ============================================================================
// RSB Size Test Chain Generator
// ============================================================================

/**
 * Generate RSB (Return Stack Buffer) size test chain
 *
 * Creates a chain of nested functions for measuring RSB depth:
 * - fun[0]: just returns
 * - fun[1]: calls fun[0], then returns
 * - fun[2]: calls fun[1], then returns
 * - ...
 * - fun[depth-1]: calls fun[depth-2], then returns
 *
 * Uses PC-relative JAL instructions for compact code generation.
 * Timing the execution of fun[N] reveals when the RSB overflows
 * (performance cliff indicates RSB size).
 *
 * @param buf    JIT buffer to emit code into (must have capacity for ~4*depth instructions)
 * @param depth  Number of functions in chain (depth of nesting)
 * @return Array of function pointers [fun0, fun1, ..., fun[depth-1]] (caller must free)
 */
static inline void** jit_rsb_chain(jit_buf_t* buf, size_t depth) {
    void** funcs = malloc(depth * sizeof(void*));
    if (!funcs) return NULL;

    /* Generate each function in sequence */
    for (size_t i = 0; i < depth; i++) {
        /* Record starting position of this function */
        funcs[i] = jit_get_pc(buf);

        if (i == 0) {
            /* Base case: fun0() just returns */
            jit_ret(buf);
        } else {
            /* Recursive case: call previous function then return */
            /* Stack frame: save/restore return address */
            jit_addi(buf, SP, SP, -16);     /* addi sp, sp, -16 */
            jit_sd(buf, RA, SP, 0);         /* sd ra, 0(sp) */

            /* Calculate PC-relative offset to previous function */
            void* current_pc = jit_get_pc(buf);
            int32_t offset = (int32_t)((char*)funcs[i-1] - (char*)current_pc);

            /* Call previous function using PC-relative jal */
            jit_jal(buf, RA, offset);       /* jal ra, offset */

            /* Restore and return */
            jit_ld(buf, RA, SP, 0);         /* ld ra, 0(sp) */
            jit_addi(buf, SP, SP, 16);      /* addi sp, sp, 16 */
            jit_ret(buf);                   /* ret */
        }
    }

    /* Finalize all emitted code */
    jit_finalize(buf);
    return funcs;
}

#endif /* _LIBJIT_H_ */
